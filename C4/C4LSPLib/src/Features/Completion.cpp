#include "Features/Completion.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Features/TypeText.h" // FunctionSignature, TypeText
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/TypeHandle.h"   // UnwrapToValue (alias/reference -> value type)
#include "Model/Types.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <cctype>
#include <algorithm>

namespace C4LSP::Features
{
	// LSP CompletionItemKind values.
	namespace Kind { constexpr int Function = 3; constexpr int Variable = 6; constexpr int Field = 5; constexpr int Class = 7; constexpr int Method = 2; constexpr int Enum = 14; constexpr int Module = 9; }

	/// Member access sees through pointers / references / aliases (the analyzer's own rule), so
	/// completion unwraps the base type identically.
	static const Scripting::Type* Deref(const Scripting::Type* type) { return Scripting::TypeHandle::UnwrapToValue(type); }

	Core::Vector<CompletionItem> Completion::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		std::string text(source.Data(), source.Size());
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);
		std::string prefix = WordPrefix(text, position);

		size_t offset = OffsetAt(text, position);
		size_t baseEnd = FindMemberAccessBaseEnd(text, offset); // member access in progress? (a. / a->)
		const Server::SemanticResolver& resolver = session.GetResolver();

		Core::Vector<Candidate> candidates;
		if (located.Found())
		{
			if (baseEnd != std::string::npos)
			{
				// `a.` / `a->` (+ optional typed prefix): offer the base object's members. Resolve the base type
				// from its OWN enclosing scope chain first (position-aware - never another function's same-named
				// local), then fall back to a name-based lookup when no node exists at the caret yet.
				const Scripting::Type* baseType = ResolveBaseType(session, sourceId, unit->GetRootAST(), baseEnd);
				if (!baseType)
					baseType = ResolveBaseTypeByName(resolver, located.path, BaseIdentifierAt(text, offset));
				if (baseType)
					CollectMembersOf(baseType, candidates);
			}
			else
			{
				CollectEnclosingContext(located.path, candidates);
				CollectClassMembers(located.path, candidates);
			}
		}
		else if (baseEnd != std::string::npos)
		{
			// The member access has not parsed into the AST yet - a bare `a.` / `a->` is a syntax error, so
			// Locate finds no node at the caret. Resolve the base by name through its own enclosing scope chain
			// instead of a flat name scan: this is what an editor needs for the first keystroke after '.'.
			const Scripting::Type* baseType = ResolveBaseTypeByName(resolver, located.path, BaseIdentifierAt(text, offset));
			if (baseType)
				CollectMembersOf(baseType, candidates);
		}

		CollectGlobalSymbols(resolver, candidates);

		return Filter(candidates, prefix);
	}

	std::string Completion::WordPrefix(const std::string& text, const Protocol::Position& position)
	{
		size_t offset = 0;
		uint32 currentLine = 0;
		while (currentLine < position.line && offset < text.size())
		{
			size_t newline = text.find('\n', offset);
			if (newline == std::string::npos)
				break;
			offset = newline + 1;
			++currentLine;
		}
		offset += position.character;

		std::string word;
		while (offset > 0 && IsIdentChar(text[offset - 1]))
			word.insert(word.begin(), text[--offset]);
		return word;
	}

	size_t Completion::OffsetAt(const std::string& text, const Protocol::Position& position)
	{
		if (position.line == 0)
			return std::min<size_t>(position.character, text.size());
		size_t offset = 0;
		uint32 line = 0;
		while (line < position.line && offset < text.size())
		{
			size_t newline = text.find('\n', offset);
			if (newline == std::string::npos)
				break;
			offset = newline + 1;
			++line;
		}
		return std::min(offset + position.character, text.size());
	}

	size_t Completion::FindMemberAccessBaseEnd(const std::string& text, size_t offset)
	{
		if (offset == 0 || offset > text.size())
			return std::string::npos;

		size_t i = offset - 1;
		while (i > 0 && IsIdentChar(text[i])) --i; // member name being typed (`a.In|it`) -> back to the accessor
		while (i > 0 && std::isspace(static_cast<unsigned char>(text[i]))) --i; // `a . x` / `a ->x`

		bool arrow = i >= 2 && text[i] == '>' && text[i - 1] == '-';
		bool dot = !arrow && i > 0 && text[i] == '.';
		if (!arrow && !dot)
			return std::string::npos; // a lone `-` is subtraction, or `a-` not yet completed to `->`

		size_t k = i - (arrow ? 2 : 1); // last char of the base expression (may be whitespace)
		while (k > 0 && std::isspace(static_cast<unsigned char>(text[k]))) --k;
		if (!IsIdentChar(text[k])) // base must end in an identifier (`a.`, `(x + y).` unsupported)
			return std::string::npos;
		size_t baseEnd = k; // byte index of the base's last character
		while (k > 0 && IsIdentChar(text[k - 1])) --k; // walk to the start of that identifier
		if (k > 0 && !std::isspace(static_cast<unsigned char>(text[k - 1])))
			return std::string::npos; // part of a longer word (`foo_bar.` -> bar is not the base)
		return baseEnd;
	}

	std::string Completion::BaseIdentifierAt(const std::string& text, size_t offset)
	{
		if (offset == 0 || offset > text.size())
			return {};

		size_t i = offset - 1;
		while (i > 0 && IsIdentChar(text[i])) --i; // member name being typed (`a.In|it`) -> back to the accessor
		while (i > 0 && std::isspace(static_cast<unsigned char>(text[i]))) --i;

		bool arrow = i >= 2 && text[i] == '>' && text[i - 1] == '-';
		if (!arrow)
		{
			if (!(i > 0 && text[i] == '.'))
				return {}; // not a member access at all
		}

		size_t k = i - (arrow ? 2 : 1); // last char of the base expression (may be whitespace)
		while (k > 0 && std::isspace(static_cast<unsigned char>(text[k]))) --k;
		if (!IsIdentChar(text[k]))
			return {}; // base must end in an identifier (`(x + y).` unsupported by this fallback)
		size_t baseEnd = k;
		while (k > 0 && IsIdentChar(text[k - 1])) --k;
		if (k > 0 && !std::isspace(static_cast<unsigned char>(text[k - 1])))
			return {}; // part of a longer word (`foo_bar.` -> bar is not the base)
		return text.substr(k, baseEnd - k + 1);
	}

	const Scripting::Type* Completion::ResolveBaseType(Server::CompilationSession& session, uint32 sourceId,
		const Ref<Scripting::AstNode>& root, size_t baseEndIdx)
	{
		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		if (baseEndIdx >= source.Size())
			baseEndIdx = source.Size();

		uint32 line1 = 0, column1 = 0; // 1-based
		source.ByteOffsetToLineColumn(baseEndIdx, line1, column1);
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, root,
			Protocol::Position{ line1 - 1, column1 - 1 });
		const Scripting::AstNode* node = located.node;
		if (!node)
			return nullptr;

		// The caret sits right after the dot/arrow, so Locate may return the MemberAccess wrapper whose base
		// child ends BEFORE the offset. Drill into that base identifier: a valid member access resolves through
		// it, and even for a dangling one the base is in `located.path`, which feeds the resolver's context.
		if (node->Type() == Scripting::NodeType::MemberAccess)
		{
			const auto& member = static_cast<const Scripting::MemberAccessNode&>(*node);
			if (member.GetBaseObject())
				node = member.GetBaseObject().Get();
		}

		// Resolve through the shared resolver - the compiler's own resolution (locals, fields via implicit this,
		// globals, functions). A dangling base (`a.` with no member) still resolves its base identifier here.
		const Server::NodeResolution res = session.GetResolver().ResolveReference(*node, located.path);
		return res.type ? Deref(res.type) : nullptr;
	}

	const Scripting::Type* Completion::ResolveBaseTypeByName(const Server::SemanticResolver& resolver,
		const Core::Vector<const Scripting::AstNode*>& lexicalPath, const std::string& baseName)
	{
		if (baseName.empty())
			return nullptr;
		// Position-aware: the enclosing scope chain of THIS caret position decides which same-named local wins;
		// a flat name scan is only a fallback for names with no enclosing declaration at all (globals, fields).
		const Server::NodeResolution res = resolver.ResolveIdentifier(baseName, lexicalPath);
		return res.type ? Deref(res.type) : nullptr;
	}

	void Completion::CollectMembersOf(const Scripting::Type* type, Core::Vector<Candidate>& out)
	{
		if (!type)
			return;
		switch (type->GetKind())
		{
		case Scripting::TypeKind::Object: {
			const auto& obj = *static_cast<const Scripting::ObjectType*>(type);
			AddMembersOf(&obj, out); // derived first so derived members win over inherited ones
			for (const Scripting::ObjectType* base : obj.GetBaseTypes())
				if (base)
					AddMembersOf(base, out);
			break;
		}
		case Scripting::TypeKind::Union: {
			const auto& fields = static_cast<const Scripting::UnionType*>(type)->GetFields();
			for (const auto& field : fields)
				AddCandidate(out, field.mName, Kind::Field, TypeText(field.m_Type));
			break;
		}
		case Scripting::TypeKind::Enum: {
			const auto& members = static_cast<const Scripting::EnumType*>(type)->GetMembers();
			for (const auto& member : members)
				AddCandidate(out, member.mName, Kind::Enum, type->ToString());
			break;
		}
		default:
			break; // scalars / pointers to non-objects have no members
		}
	}

	void Completion::AddMembersOf(const Scripting::ObjectType* obj, Core::Vector<Candidate>& out)
	{
		for (const auto& field : obj->GetFields())
			AddCandidate(out, field.mName, Kind::Field, TypeText(field.m_Type));
		auto addMethods = [&out](const Core::Vector<Scripting::ObjectType::MethodInfo>& methods) {
			for (const auto& method : methods)
				AddCandidate(out, method.mName, Kind::Method, MethodSignature(method));
		};
		addMethods(obj->GetMethods());
		addMethods(obj->GetExtensionMethods()); // operators + CBind foreign members are extensions
	}

	std::string Completion::MethodSignature(const Scripting::ObjectType::MethodInfo& method)
	{
		std::string text = TypeText(method.m_ReturnType);
		if (method.m_ParameterTypes.Count() == 0)
			return text + "()";
		text += "(";
		for (uint32 i = 0; i < method.m_ParameterTypes.Count(); ++i)
		{
			if (i > 0)
				text += ", ";
			text += TypeText(method.m_ParameterTypes[i]);
		}
		return text + ")";
	}

	bool Completion::IsIdentChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	void Completion::CollectEnclosingContext(const Core::Vector<const Scripting::AstNode*>& path, Core::Vector<Candidate>& out)
	{
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = path[i];
			switch (node->Type())
			{
			case Scripting::NodeType::FunctionDefinition: {
				const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(*node);
				for (const auto& param : func.GetParameters())
					AddCandidate(out, param->GetNameToken().GetText(), Kind::Variable,
						param->GetType() ? param->GetType()->ToString() : "");
				break;
			}
			case Scripting::NodeType::CodeBlock: {
				const auto& block = static_cast<const Scripting::CodeBlockNode&>(*node);
				for (const auto& stmt : block.GetStatements())
					if (stmt && stmt->Type() == Scripting::NodeType::VariableDefinition)
					{
						const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(*stmt.Get());
						AddCandidate(out, var.GetNameToken().GetText(), Kind::Variable,
							var.GetType() ? var.GetType()->ToString() : "");
					}
				break;
			}
			default:
				break;
			}
		}
	}

	void Completion::CollectClassMembers(const Core::Vector<const Scripting::AstNode*>& path, Core::Vector<Candidate>& out)
	{
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = path[i];
			if (node->Type() != Scripting::NodeType::ObjectDefinition)
				continue;

			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(*node);
			for (const auto& member : obj.GetBodyMembers())
			{
				if (!member)
					continue;
				switch (member->Type())
				{
				case Scripting::NodeType::VariableDefinition: {
					const auto& field = static_cast<const Scripting::VariableDefinitionNode&>(*member.Get());
					AddCandidate(out, field.GetNameToken().GetText(), Kind::Field,
						field.GetType() ? field.GetType()->ToString() : "");
					break;
				}
				case Scripting::NodeType::FunctionDefinition: {
					const auto& method = static_cast<const Scripting::FunctionDefinitionNode&>(*member.Get());
					if (method.GetName())
						AddCandidate(out, method.GetName()->GetFirstIdentifierToken().GetText(), Kind::Method,
							method.GetReturnType().Get() ? method.GetReturnType()->ToString() + "()" : "()");
					break;
				}
				default:
					break;
				}
			}
			break; // only the innermost class
		}
	}

	void Completion::CollectGlobalSymbols(const Server::SemanticResolver& resolver, Core::Vector<Candidate>& out)
	{
		std::vector<Server::SemanticResolver::GlobalSymbol> symbols;
		resolver.CollectGlobalSymbols(symbols);
		for (const auto& gs : symbols)
		{
			std::string detail = gs.fn ? FunctionSignature(gs.fn->type)
				: (gs.type ? gs.type->ToString() : "");
			AddCandidate(out, gs.name, SymbolKindToLsp(gs.kind), detail);
		}
	}

	void Completion::AddCandidate(Core::Vector<Candidate>& out, const std::string& name, int kind, const std::string& detail)
	{
		if (name.empty())
			return;
		for (const auto& existing : out)
			if (existing.name == name)
				return; // innermost scope wins
		out.Push(Candidate{ name, kind, detail });
	}

	Core::Vector<CompletionItem> Completion::Filter(const Core::Vector<Candidate>& candidates, const std::string& prefix)
	{
		Core::Vector<CompletionItem> result;
		for (const auto& candidate : candidates)
		{
			if (!prefix.empty() && !StartsWith(candidate.name, prefix))
				continue;
			result.Push(CompletionItem{ candidate.name, candidate.kind, candidate.detail });
		}
		return result;
	}

	bool Completion::StartsWith(const std::string& name, const std::string& prefix)
	{
		if (prefix.size() > name.size())
			return false;
		for (size_t i = 0; i < prefix.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
				return false;
		return true;
	}

	int Completion::SymbolKindToLsp(Scripting::SymbolKind kind)
	{
		switch (kind)
		{
		case Scripting::SymbolKind::Function:  return Kind::Function;
		case Scripting::SymbolKind::Type:      return Kind::Class;
		case Scripting::SymbolKind::Namespace: return Kind::Module;
		case Scripting::SymbolKind::Variable:  return Kind::Variable;
		default:                               return Kind::Variable;
		}
	}
} // namespace C4LSP::Features
