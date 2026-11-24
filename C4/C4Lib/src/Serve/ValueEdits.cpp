#include "Serve/ValueEdits.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DeclarationFingerprint.h"
#include "Lexer/Lexer.h"
#include "Model/ConstEvaluator.h"
#include "Model/ConstantValue.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace C4Compiler
{
	namespace
	{
		// The bytes [begin, end) of one file; for a node a macro writes, the use in the file and what it spells there.
		struct Range
		{
			uint32 sourceId = 0;
			uint32 begin = 0;
			uint32 end = 0;

			bool Holds(uint32 at) const { return at >= begin && at < end; }
		};

		Range RangeOf(const AstNode& node)
		{
			const uint32 a = node.startLocation.byteOffset;
			const uint32 b = node.endLocation.byteOffset;
			return { node.startLocation.sourceId, std::min(a, b), std::max(a, b) };
		}

		std::string NameOf(const SymbolInfo& global)
		{
			return (global.plainName.empty() ? global.mangledName : global.plainName).c_str();
		}

		bool IdentifierChar(char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
		}

		// Where `name` is written in `text` as a word - in code, a comment, a string or a directive alike.
		std::vector<uint32> WordsAt(std::string_view text, std::string_view name)
		{
			std::vector<uint32> at;
			for (size_t found = text.find(name); found != std::string_view::npos; found = text.find(name, found + 1))
			{
				const size_t end = found + name.size();
				if ((found == 0 || !IdentifierChar(text[found - 1])) && (end == text.size() || !IdentifierChar(text[end])))
					at.push_back(static_cast<uint32>(found));
			}
			return at;
		}

		bool NamesAny(std::string_view text, const std::vector<std::string>& names)
		{
			return std::any_of(names.begin(), names.end(), [&](const std::string& name) { return !WordsAt(text, name).empty(); });
		}

		std::string_view TextOf(const CompilerInstance& compiler, uint32 sourceId)
		{
			const Ref<CompilationUnit> unit = compiler.FindCompilationUnit(sourceId);
			return unit && unit->GetSource() ? std::string_view(unit->GetSource()->Text()) : std::string_view();
		}

		std::string_view Slice(std::string_view text, const Range& range)
		{
			if (range.begin >= text.size()) return {};
			return text.substr(range.begin, std::min<size_t>(range.end, text.size()) - range.begin);
		}

		// Every global whose initializer names one of `names`, on to the end: each joins reach.globals, its name `names`.
		std::string AddDependents(const CompilerInstance& compiler, const ProgramModel& model, std::vector<std::string>& names, ValueReach& reach)
		{
			std::unordered_set<const SymbolInfo*> folded(reach.globals.begin(), reach.globals.end());
			std::string why;
			for (bool grew = true; grew && why.empty();)
			{
				grew = false;
				model.Symbols()->ForEachScopedSymbol([&](const Core::String&, SymbolInfo* info, const Scope&) {
					if (!why.empty() || info->kind != SymbolKind::Variable || !info->initializer || folded.count(info)) return;
					const Range range = RangeOf(*info->initializer);
					if (!NamesAny(Slice(TextOf(compiler, range.sourceId), range), names)) return;
					if (info->constValue.IsNone())
					{
						why = "'" + NameOf(*info) + "' starts at run time from a value the edit changes";
						return;
					}
					folded.insert(info);
					reach.globals.push_back(info);
					names.push_back(NameOf(*info));
					grew = true;
				});
			}
			return why;
		}

		// A function body of a file: who it is, and whether a constant may run it.
		struct Body
		{
			Range range;
			const FunctionDefinitionNode* function = nullptr;
		};

		// A file's function bodies, and where its variables outside them are declared: a name written there declares a
		// variable (another of the same name, in another namespace or class), it reads no value.
		struct Places
		{
			std::vector<Body> bodies;
			std::vector<uint32> declaredNames;
		};

		void CollectPlaces(const AstNode& node, Places& places)
		{
			if (node.Type() == NodeType::FunctionDefinition)
			{
				const auto& fn = static_cast<const FunctionDefinitionNode&>(node);
				if (fn.GetBody()) places.bodies.push_back({ RangeOf(*fn.GetBody()), &fn });
				return; // a body's own functions and variables are within it
			}
			if (node.Type() == NodeType::VariableDefinition)
			{
				for (const Declarator& declarator : static_cast<const VariableDefinitionNode&>(node).GetDeclarators())
					if (const SourceLocation& at = declarator.m_NameToken.GetStartLocation(); !at.expansion) places.declaredNames.push_back(at.byteOffset);
				return;
			}
			for (const Ref<AstNode>& child : node.Children())
				if (child) CollectPlaces(*child, places);
		}

		// A token of a file, by the bytes of the file it stands for.
		struct FileToken
		{
			TokenType type = TokenType::EndOfFile;
			Core::String text;
			uint32 begin = 0;
			uint32 end = 0;
			bool expanded = false;
		};

		std::vector<FileToken> TokensOf(const CompilationUnit& unit)
		{
			std::vector<FileToken> tokens;
			Lexer lexer(*unit.GetSource(), unit.SourceId(), unit.GetMacros());
			for (const Token* token = &lexer.NextToken(); token->GetType() != TokenType::EndOfFile; token = &lexer.NextToken())
				tokens.push_back({ token->GetType(), token->GetText(), token->GetStartLocation().byteOffset, token->GetEndLocation().byteOffset,
					token->GetStartLocation().expansion != 0 });
			return tokens;
		}

		// Text no name in which reads a value: a comment, a string or a character.
		bool Inert(const FileToken& token)
		{
			switch (token.type)
			{
			case TokenType::Comment:
			case TokenType::MultiComment:
			case TokenType::Text:
			case TokenType::Char: return !token.expanded;
			default: return false;
			}
		}

		uint32 LineOf(std::string_view text, uint32 at)
		{
			return 1 + static_cast<uint32>(std::count(text.begin(), text.begin() + std::min<size_t>(at, text.size()), '\n'));
		}

		// Where the names of reach.globals are read, file by file: each read is in a function body, whose lowered functions
		// read it, or in an initializer of a global folded again, or names such a global where it is declared.
		class Reads
		{
		public:
			Reads(const CompilerInstance& compiler, const ValueReach& reach, const std::vector<std::string>& names,
				const std::vector<const FunctionRecord*>& lowered)
				: m_Compiler(compiler), m_Names(names), m_Lowered(lowered), m_Reads(lowered.size(), false)
			{
				for (const SymbolInfo* global : reach.globals) Allow(*global);
				for (size_t i = 0; i < lowered.size(); ++i)
					if (const AstNode* body = lowered[i]->bodyNode) m_LoweredBodies[body->startLocation.sourceId].push_back({ RangeOf(*body), i });
			}

			// Why a read in `unit` is not served; empty when none is.
			std::string In(const CompilationUnit& unit)
			{
				const std::string_view text = unit.GetSource()->Text();
				std::vector<std::pair<uint32, const std::string*>> written;
				for (const std::string& name : m_Names)
					for (uint32 at : WordsAt(text, name)) written.push_back({ at, &name });
				if (written.empty()) return {};
				const std::vector<FileToken> tokens = TokensOf(unit);
				Places places;
				for (const Ref<AstNode>& child : unit.GetOriginalChildren())
					if (child) CollectPlaces(*child, places);
				for (const auto& [at, name] : written)
					if (std::string why = Read(unit, text, tokens, places, at, *name); !why.empty()) return why;
				return {};
			}

			std::vector<const FunctionRecord*> Readers() const
			{
				std::vector<const FunctionRecord*> readers;
				for (size_t i = 0; i < m_Lowered.size(); ++i)
					if (m_Reads[i]) readers.push_back(m_Lowered[i]);
				return readers;
			}

		private:
			struct LoweredBody
			{
				Range range;
				size_t index = 0;
			};

			void Allow(const SymbolInfo& global)
			{
				if (global.initializer) m_Allowed.push_back(RangeOf(*global.initializer));
				if (const auto* var = dynamic_cast<const VariableDefinitionNode*>(global.declNode))
					for (const Declarator& declarator : var->GetDeclarators())
					{
						const SourceLocation& at = declarator.m_NameToken.GetStartLocation();
						m_Allowed.push_back({ at.sourceId, at.byteOffset, at.byteOffset + static_cast<uint32>(declarator.m_NameToken.GetText().size()) });
					}
			}

			bool Allowed(uint32 sourceId, uint32 at) const
			{
				return std::any_of(m_Allowed.begin(), m_Allowed.end(), [&](const Range& range) { return range.sourceId == sourceId && range.Holds(at); });
			}

			std::string Where(const CompilationUnit& unit, std::string_view text, uint32 at) const
			{
				return m_Compiler.GetSourceManager().GetInfo(unit.SourceId()).displayName + " line " + std::to_string(LineOf(text, at));
			}

			// The name written at `at`: an identifier read where the edit is served, or text that reads nothing.
			std::string Read(const CompilationUnit& unit, std::string_view text, const std::vector<FileToken>& tokens, const Places& places,
				uint32 at, const std::string& name)
			{
				const std::vector<Body>& bodies = places.bodies;
				const bool identifier = std::any_of(tokens.begin(), tokens.end(), [&](const FileToken& token) {
					return token.type == TokenType::Identifier && token.begin == at && token.text == name;
				});
				if (!identifier)
				{
					if (std::any_of(tokens.begin(), tokens.end(), [&](const FileToken& token) { return Inert(token) && at >= token.begin && at < token.end; }))
						return {};
					return "'" + name + "' is named by a macro or a directive (" + Where(unit, text, at) + ")";
				}
				const uint32 file = unit.SourceId();
				if (Allowed(file, at)) return {};
				if (std::find(places.declaredNames.begin(), places.declaredNames.end(), at) != places.declaredNames.end()) return {};
				const auto body = std::find_if(bodies.begin(), bodies.end(), [&](const Body& b) { return b.range.sourceId == file && b.range.Holds(at); });
				if (body == bodies.end()) return "'" + name + "' is read outside a function body (" + Where(unit, text, at) + ")";
				if (IsCompileTime(*body->function)) return "'" + name + "' is read by a [compile_time] function, which a constant may run (" + Where(unit, text, at) + ")";
				if (const auto lowered = m_LoweredBodies.find(file); lowered != m_LoweredBodies.end())
					for (const LoweredBody& reader : lowered->second)
						if (reader.range.Holds(at)) m_Reads[reader.index] = true;
				return {};
			}

			const CompilerInstance& m_Compiler;
			const std::vector<std::string>& m_Names;
			const std::vector<const FunctionRecord*>& m_Lowered;
			std::vector<bool> m_Reads;                                          // by m_Lowered's index
			std::vector<Range> m_Allowed;                                       // the globals' initializers and declared names
			std::unordered_map<uint32, std::vector<LoweredBody>> m_LoweredBodies; // by file
		};
	}

	std::string ReachOfValues(const CompilerInstance& compiler, const ProgramModel& model, const std::vector<SymbolInfo*>& edited,
		const std::vector<const FunctionRecord*>& lowered, ValueReach& reach)
	{
		reach.globals = edited;
		std::vector<std::string> names;
		for (const SymbolInfo* global : edited) names.push_back(NameOf(*global));
		if (std::string why = AddDependents(compiler, model, names, reach); !why.empty()) return why;
		Reads reads(compiler, reach, names, lowered);
		for (uint32 id = 1; id <= compiler.SourceCount(); ++id)
			if (const Ref<CompilationUnit> unit = compiler.FindCompilationUnit(id); unit && unit->GetSource())
				if (std::string why = reads.In(*unit); !why.empty()) return why;
		reach.readers = reads.Readers();
		return {};
	}

	std::string FoldAgain(const ProgramModel& model, const std::vector<SymbolInfo*>& globals)
	{
		std::vector<ConstantValue> was;
		for (SymbolInfo* global : globals)
		{
			was.push_back(global->constValue);
			global->constValue = ConstantValue{};
		}
		for (size_t i = 0; i < globals.size(); ++i)
		{
			SymbolInfo& global = *globals[i];
			const auto folded = ConstEvaluator(model, global.declaringScope).EvaluateStart(global.initializer, global.type, global.definingUnit);
			if (!folded.Ok() || folded.value.IsNone()) return "'" + NameOf(global) + "' no longer folds to a value: a cold build starts it at run time";
			if (folded.value.kind != was[i].kind || folded.value.unitWidth != was[i].unitWidth) return "'" + NameOf(global) + "' folds to a value of another kind now";
			global.constValue = folded.value;
		}
		return {};
	}
} // namespace C4Compiler
