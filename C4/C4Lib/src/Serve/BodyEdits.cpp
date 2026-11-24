#include "Serve/BodyEdits.h"
#include "Compiler/DeclarationFingerprint.h"
#include "Lexer/Lexer.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace C4Compiler
{
	namespace
	{
		// The bytes [begin, end) of a body in its file.
		struct Span
		{
			uint32 begin = 0;
			uint32 end = 0;
		};

		// A token as the lexer reads it through the file's macro layout.
		struct ReadToken
		{
			TokenType type = TokenType::EndOfFile;
			Core::String text;
			SourceLocation at;
		};

		BodyEdit Refused(std::string why)
		{
			BodyEdit edit;
			edit.refusal = std::move(why);
			return edit;
		}

		// A file's function definitions in its order; one a body declares is that body's.
		void CollectDefinitions(const AstNode& node, std::vector<const FunctionDefinitionNode*>& out)
		{
			if (node.Type() == NodeType::FunctionDefinition)
			{
				out.push_back(static_cast<const FunctionDefinitionNode*>(&node));
				return;
			}
			for (const Ref<AstNode>& child : node.Children())
				if (child) CollectDefinitions(*child, out);
		}

		std::vector<const FunctionDefinitionNode*> DefinitionsOf(const CompilationUnit& unit)
		{
			std::vector<const FunctionDefinitionNode*> definitions;
			for (const Ref<AstNode>& child : unit.GetOriginalChildren())
				if (child) CollectDefinitions(*child, definitions);
			return definitions;
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

		// The bodies their own functions alone read, as spans of the file, each with its definition's index; or why a
		// body does not keep its place.
		std::string OwnBodies(const std::vector<const FunctionDefinitionNode*>& was, const std::vector<const FunctionDefinitionNode*>& is,
			std::vector<size_t>& owners, std::vector<Span>& wasSpans, std::vector<Span>& isSpans)
		{
			for (size_t i = 0; i < was.size(); ++i)
			{
				const bool own = BodyIsItsOwn(*was[i]);
				if (own != BodyIsItsOwn(*is[i])) return "a function's kind changed";
				if (!own) continue;
				const SourceLocation& start = was[i]->GetBody()->startLocation;
				const SourceLocation& end = was[i]->GetBody()->endLocation;
				const SourceLocation& newStart = is[i]->GetBody()->startLocation;
				const SourceLocation& newEnd = is[i]->GetBody()->endLocation;
				if (start.expansion || end.expansion || newStart.expansion || newEnd.expansion) return "a macro writes a body";
				owners.push_back(i);
				wasSpans.push_back({ start.byteOffset, end.byteOffset });
				isSpans.push_back({ newStart.byteOffset, newEnd.byteOffset });
			}
			return {};
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

		// Where (in the file before) the first token outside the bodies moved; the tokens are the same.
		SourceLocation FirstMoved(const std::vector<ReadToken>& was, const std::vector<ReadToken>& is)
		{
			for (size_t i = 0; i < was.size(); ++i)
				if (!SamePlace(was[i].at, is[i].at)) return was[i].at;
			return {};
		}

		bool Before(const SourceLocation& a, const SourceLocation& b)
		{
			return a.line < b.line || (a.line == b.line && a.column <= b.column);
		}

		// What may follow a line a body edit added or removed: functions whose own lowering alone reads their bodies, which
		// are lowered again where they are now, and declarations of functions - whose places nothing emits but their
		// records, which take the new definitions. A type, a global, a template or the like that moved would keep its old
		// place in the model, which debug information and reflection's scope read.
		std::string OnlyFunctionsAfter(const AstNode& node, const SourceLocation& moved)
		{
			if (Before(node.endLocation, moved)) return {};
			if (node.Type() == NodeType::FunctionDefinition)
			{
				const auto& fn = static_cast<const FunctionDefinitionNode&>(node);
				return fn.IsDefinition() && !BodyIsItsOwn(fn) ? "an inline, [compile_time] or template function follows a moved line" : std::string();
			}
			if (node.Type() != NodeType::Namespace) return "a declaration other than a function follows a moved line";
			if (!static_cast<const NamespaceNode&>(node).GetAnnotations().Empty()) return "an annotated namespace holds a moved line";
			for (const Ref<AstNode>& child : node.Children())
				if (child)
					if (std::string why = OnlyFunctionsAfter(*child, moved); !why.empty()) return why;
			return {};
		}
	}

	BodyEdit FindBodyEdit(const CompilationUnit& before, const CompilationUnit& after)
	{
		if (!before.GetSource() || !after.GetSource()) return Refused("it has no text");
		if (after.GetErrorTracker().HasErrors()) return Refused("it does not parse");
		if (before.Fingerprint() != after.Fingerprint()) return Refused("its declarations changed");
		const std::vector<const FunctionDefinitionNode*> was = DefinitionsOf(before);
		const std::vector<const FunctionDefinitionNode*> is = DefinitionsOf(after);
		if (was.size() != is.size()) return Refused("its functions changed");
		std::vector<size_t> owners;
		std::vector<Span> wasSpans, isSpans;
		if (std::string why = OwnBodies(was, is, owners, wasSpans, isSpans); !why.empty()) return Refused(why);

		std::vector<ReadToken> wasOutside, isOutside;
		std::vector<std::vector<ReadToken>> wasBodies, isBodies;
		Split(TokensOf(before), wasSpans, wasOutside, wasBodies);
		Split(TokensOf(after), isSpans, isOutside, isBodies);
		if (!SameText(wasOutside, isOutside)) return Refused("its tokens outside the bodies changed");
		if (TextOutside(before.GetSource()->Text(), wasSpans) != TextOutside(after.GetSource()->Text(), isSpans))
			return Refused("its text outside the bodies changed");

		BodyEdit edit;
		if (!SameTokens(wasOutside, isOutside))
		{
			edit.movedFrom = FirstMoved(wasOutside, isOutside);
			for (const Ref<AstNode>& child : before.GetOriginalChildren())
				if (child)
					if (std::string why = OnlyFunctionsAfter(*child, edit.movedFrom); !why.empty()) return Refused(why);
		}
		for (size_t i = 0; i < was.size(); ++i) edit.definitions.push_back({ was[i], is[i], false, false });
		for (size_t body = 0; body < owners.size(); ++body)
		{
			if (SameTokens(wasBodies[body], isBodies[body])) continue;
			KeptDefinition& kept = edit.definitions[owners[body]];
			kept.bodyChanged = true;
			kept.moved = SameText(wasBodies[body], isBodies[body]);
			if (!kept.moved && (MakesItsOwn(*kept.before->GetBody()) || MakesItsOwn(*kept.after->GetBody())))
				return Refused("a changed body writes a lambda or declares a local type or function");
			if (kept.moved && DeclaresLocals(*kept.before->GetBody())) return Refused("a moved body declares a local type or function");
		}
		return edit;
	}
} // namespace C4Compiler
