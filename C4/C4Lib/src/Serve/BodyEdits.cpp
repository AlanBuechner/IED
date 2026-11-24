#include "Serve/BodyEdits.h"
#include "Compiler/DeclarationFingerprint.h"
#include "Lexer/Lexer.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <algorithm>

namespace C4Compiler
{
	namespace
	{
		// What a span of the file holds: a body its own function alone reads, or a global's initializer.
		enum class SpanKind { Body, Value };

		// The bytes [begin, end) of a body or an initializer in its file, and whose it is (an index into the file's
		// definitions or globals).
		struct Span
		{
			uint32 begin = 0;
			uint32 end = 0;
			SpanKind kind = SpanKind::Body;
			size_t owner = 0;
		};

		// A token as the lexer reads it through the file's macro layout.
		struct ReadToken
		{
			TokenType type = TokenType::EndOfFile;
			Core::String text;
			SourceLocation at;
		};

		// What a variable definition is where it stands: a global of a namespace, a member of a class (a static one is a
		// global), or neither.
		enum class Place { Namespace, Class, Other };

		// The file's function definitions and globals in its order, with the spans of the bodies their own functions
		// alone read and of the globals' initializers.
		struct Layout
		{
			std::vector<const FunctionDefinitionNode*> definitions;
			std::vector<const VariableDefinitionNode*> globals;
			std::vector<Span> spans;
			std::string refusal;
		};

		FileEdit Refused(std::string why)
		{
			FileEdit edit;
			edit.refusal = std::move(why);
			return edit;
		}

		bool MacroPlaced(const AstNode& node)
		{
			return node.startLocation.expansion || node.endLocation.expansion;
		}

		// A body whose edit lowering its functions again serves: one its own lowering alone reads, an inline function's,
		// which the partitions that copy it take from its lowered function, and a template's, each of whose lowered
		// instances is lowered again - but a [compile_time] function's, which constants run (and which alone calls its
		// kind, so no constant runs another body).
		bool BodyIsServed(const FunctionDefinitionNode& fn)
		{
			return fn.IsDefinition() && !IsCompileTime(fn);
		}

		void AddDefinition(Layout& layout, const FunctionDefinitionNode& fn)
		{
			layout.definitions.push_back(&fn);
			if (!BodyIsServed(fn)) return;
			const AstNode& body = *fn.GetBody();
			if (MacroPlaced(body)) layout.refusal = "a macro writes a body";
			layout.spans.push_back({ body.startLocation.byteOffset, body.endLocation.byteOffset, SpanKind::Body, layout.definitions.size() - 1 });
		}

		// An initializer a macro writes has no span: an edit of it is an edit outside.
		void AddGlobal(Layout& layout, const VariableDefinitionNode& var)
		{
			layout.globals.push_back(&var);
			for (const Declarator& declarator : var.GetDeclarators())
				if (declarator.m_Initializer && !MacroPlaced(*declarator.m_Initializer))
					layout.spans.push_back({ declarator.m_Initializer->startLocation.byteOffset, declarator.m_Initializer->endLocation.byteOffset,
						SpanKind::Value, layout.globals.size() - 1 });
		}

		// A class template's static members belong to its instances: they are no globals of the file.
		Place PlaceIn(const AstNode& node)
		{
			if (node.Type() == NodeType::Namespace) return Place::Namespace;
			if (node.Type() == NodeType::ObjectDefinition)
				return static_cast<const ObjectDefinitionNode&>(node).GetTemplateDefinition() ? Place::Other : Place::Class;
			return Place::Other;
		}

		// A body declares nothing the file's layout holds: a function's own definitions are its body's.
		void Collect(Layout& layout, const AstNode& node, Place place)
		{
			if (node.Type() == NodeType::FunctionDefinition)
			{
				AddDefinition(layout, static_cast<const FunctionDefinitionNode&>(node));
				return;
			}
			if (node.Type() == NodeType::VariableDefinition)
			{
				const auto& var = static_cast<const VariableDefinitionNode&>(node);
				if (place == Place::Namespace || (place == Place::Class && var.IsStatic())) AddGlobal(layout, var);
				return;
			}
			const Place inner = PlaceIn(node);
			for (const Ref<AstNode>& child : node.Children())
				if (child) Collect(layout, *child, inner);
		}

		Layout LayoutOf(const CompilationUnit& unit)
		{
			Layout layout;
			for (const Ref<AstNode>& child : unit.GetOriginalChildren())
				if (child) Collect(layout, *child, Place::Namespace);
			std::stable_sort(layout.spans.begin(), layout.spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
			return layout;
		}

		bool SameSpans(const std::vector<Span>& a, const std::vector<Span>& b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].kind != b[i].kind || a[i].owner != b[i].owner) return false;
			return true;
		}

		std::vector<ReadToken> TokensOf(const CompilationUnit& unit)
		{
			std::vector<ReadToken> tokens;
			Lexer lexer(*unit.GetSource(), unit.SourceId(), unit.GetMacros());
			for (const Token* token = &lexer.NextToken(); token->GetType() != TokenType::EndOfFile; token = &lexer.NextToken())
				tokens.push_back({ token->GetType(), token->GetText(), token->GetStartLocation() });
			return tokens;
		}

		bool SamePlace(const SourceLocation& a, const SourceLocation& b)
		{
			return a.line == b.line && a.column == b.column && a.expansion == b.expansion && a.inExpansion == b.inExpansion;
		}

		bool SameToken(const ReadToken& a, const ReadToken& b)
		{
			return a.type == b.type && a.text == b.text && SamePlace(a.at, b.at);
		}

		// A local type or function: lowering the body again would make it again beside the old one.
		bool DeclaresLocals(const AstNode& node)
		{
			switch (node.Type())
			{
			case NodeType::FunctionDefinition:
			case NodeType::ObjectDefinition:
			case NodeType::EnumDefinition:
			case NodeType::UnionDefinition: return true;
			default: break;
			}
			for (const Ref<AstNode>& child : node.Children())
				if (child && DeclaresLocals(*child)) return true;
			return false;
		}

		// What lowering a body makes in the model of its own: a local type or function, and a lambda's closure, which a
		// changed lambda would need anew.
		bool MakesItsOwn(const AstNode& node)
		{
			if (node.Type() == NodeType::LambdaExpression || DeclaresLocals(node)) return true;
			for (const Ref<AstNode>& child : node.Children())
				if (child && MakesItsOwn(*child)) return true;
			return false;
		}

		// The tokens outside the spans, and those of each span.
		void Split(const std::vector<ReadToken>& tokens, const std::vector<Span>& spans, std::vector<ReadToken>& outside,
			std::vector<std::vector<ReadToken>>& inside)
		{
			inside.assign(spans.size(), {});
			size_t span = 0;
			for (const ReadToken& token : tokens)
			{
				const uint32 at = token.at.byteOffset;
				while (span < spans.size() && at >= spans[span].end) ++span;
				if (span < spans.size() && at >= spans[span].begin) inside[span].push_back(token);
				else outside.push_back(token);
			}
		}

		// The file's text outside the spans, piece by piece.
		std::vector<std::string_view> TextOutside(std::string_view text, const std::vector<Span>& spans)
		{
			std::vector<std::string_view> pieces;
			uint32 from = 0;
			for (const Span& span : spans)
			{
				pieces.push_back(text.substr(from, span.begin - from));
				from = span.end;
			}
			pieces.push_back(text.substr(from));
			return pieces;
		}

		bool SameTokens(const std::vector<ReadToken>& a, const std::vector<ReadToken>& b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (!SameToken(a[i], b[i])) return false;
			return true;
		}

		// The same tokens, wherever they are.
		bool SameText(const std::vector<ReadToken>& a, const std::vector<ReadToken>& b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (a[i].type != b[i].type || a[i].text != b[i].text) return false;
			return true;
		}

		bool Within(const AstNode& node, uint32 byteOffset)
		{
			return byteOffset >= node.startLocation.byteOffset && byteOffset < node.endLocation.byteOffset;
		}

		// Where (in the file before) the first token outside the bodies and initializers moved, the rest of an edited
		// global's own definition left out (the global takes its new definition); the tokens are the same.
		SourceLocation FirstMoved(const std::vector<ReadToken>& was, const std::vector<ReadToken>& is, const FileEdit& edit)
		{
			for (size_t i = 0; i < was.size(); ++i)
			{
				if (SamePlace(was[i].at, is[i].at)) continue;
				const bool ownDefinition = std::any_of(edit.globals.begin(), edit.globals.end(), [&](const KeptGlobal& global) {
					return global.valueChanged && Within(*global.before, was[i].at.byteOffset);
				});
				if (!ownDefinition) return was[i].at;
			}
			return {};
		}

		bool Before(const SourceLocation& a, const SourceLocation& b)
		{
			return a.line < b.line || (a.line == b.line && a.column <= b.column);
		}

		// What may follow a line a body edit added or removed: functions whose bodies a served edit lowers again, which are
		// lowered again where they are now, and declarations of functions - whose places nothing emits but their records,
		// which take the new definitions. A type, a global, a template or the like that moved would keep its old place in the
		// model, which debug information and reflection's scope read.
		std::string OnlyFunctionsAfter(const AstNode& node, const SourceLocation& moved)
		{
			if (Before(node.endLocation, moved)) return {};
			if (node.Type() == NodeType::FunctionDefinition)
			{
				const auto& fn = static_cast<const FunctionDefinitionNode&>(node);
				return fn.IsDefinition() && !BodyIsServed(fn) ? "a [compile_time] or template function follows a moved line" : std::string();
			}
			if (node.Type() != NodeType::Namespace) return "a declaration other than a function follows a moved line";
			if (!static_cast<const NamespaceNode&>(node).GetAnnotations().Empty()) return "an annotated namespace holds a moved line";
			for (const Ref<AstNode>& child : node.Children())
				if (child)
					if (std::string why = OnlyFunctionsAfter(*child, moved); !why.empty()) return why;
			return {};
		}

		// A changed span's body or global. An initializer whose tokens only moved is no value edit: its definition moved with
		// what is around it, which the moved line's rule decides.
		std::string Changed(FileEdit& edit, const Span& span, const std::vector<ReadToken>& was, const std::vector<ReadToken>& is)
		{
			if (span.kind == SpanKind::Value)
			{
				if (!SameText(was, is)) edit.globals[span.owner].valueChanged = true;
				return {};
			}
			KeptDefinition& kept = edit.definitions[span.owner];
			kept.bodyChanged = true;
			kept.moved = SameText(was, is);
			if (!kept.moved && (MakesItsOwn(*kept.before->GetBody()) || MakesItsOwn(*kept.after->GetBody())))
				return "a changed body writes a lambda or declares a local type or function";
			if (kept.moved && DeclaresLocals(*kept.before->GetBody())) return "a moved body declares a local type or function";
			return {};
		}
	}

	FileEdit FindFileEdit(const CompilationUnit& before, const CompilationUnit& after)
	{
		if (!before.GetSource() || !after.GetSource()) return Refused("it has no text");
		if (after.GetErrorTracker().HasErrors()) return Refused("it does not parse");
		const Layout was = LayoutOf(before);
		const Layout is = LayoutOf(after);
		if (!was.refusal.empty()) return Refused(was.refusal);
		if (!is.refusal.empty()) return Refused(is.refusal);
		if (was.definitions.size() != is.definitions.size()) return Refused("its functions changed");
		if (was.globals.size() != is.globals.size()) return Refused("its globals changed");
		if (!SameSpans(was.spans, is.spans)) return Refused("a function's kind or a global's initializers changed");

		std::vector<ReadToken> wasOutside, isOutside;
		std::vector<std::vector<ReadToken>> wasInside, isInside;
		Split(TokensOf(before), was.spans, wasOutside, wasInside);
		Split(TokensOf(after), is.spans, isOutside, isInside);
		if (!SameText(wasOutside, isOutside)) return Refused("its declarations changed");
		if (TextOutside(before.GetSource()->Text(), was.spans) != TextOutside(after.GetSource()->Text(), is.spans))
			return Refused("its text outside the bodies and initializers changed");

		FileEdit edit;
		for (size_t i = 0; i < was.definitions.size(); ++i) edit.definitions.push_back({ was.definitions[i], is.definitions[i], false, false });
		for (size_t i = 0; i < was.globals.size(); ++i) edit.globals.push_back({ was.globals[i], is.globals[i], false });
		for (size_t span = 0; span < was.spans.size(); ++span)
			if (!SameTokens(wasInside[span], isInside[span]))
				if (std::string why = Changed(edit, was.spans[span], wasInside[span], isInside[span]); !why.empty()) return Refused(why);
		if (!SameTokens(wasOutside, isOutside))
		{
			edit.movedFrom = FirstMoved(wasOutside, isOutside, edit);
			if (edit.movedFrom.line)
				for (const Ref<AstNode>& child : before.GetOriginalChildren())
					if (child)
						if (std::string why = OnlyFunctionsAfter(*child, edit.movedFrom); !why.empty()) return Refused(why);
		}
		return edit;
	}
} // namespace C4Compiler
