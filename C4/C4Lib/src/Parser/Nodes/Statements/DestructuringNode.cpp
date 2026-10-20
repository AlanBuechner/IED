#include "Parser/Nodes/Statements/DestructuringNode.h"
#include "Parser/PrattParser.h"
#include "Compiler/ErrorTracker.h"

namespace Scripting
{
	namespace
	{
		constexpr const char* kDiscard = "_";
		constexpr uint32 kMaxLookahead = 512;

		bool EndsElement(TokenType type) { return type == TokenType::Comma || type == TokenType::CloseBracket; }

		void Expect(ParserState& state, TokenType type, const char* what)
		{
			Lexer& lexer = state.GetLexer();
			const Token t = lexer.PeekCompound();
			if (t.GetType() == type) { lexer.NextCompoundToken(); return; }
			state.GetErrorTracker().AddError(t.GetStartLocation(), Core::StringFormat::Format("Expected {0}, got '{1}'", what, t.GetText()),
				ErrorSeverity::Error);
		}
	}

	Ref<DestructuringNode> DestructuringNode::TryParse(ParserState& state)
	{
		if (!Ahead(state.GetLexer())) return nullptr;
		auto node = CreateRef<DestructuringNode>();
		node->ParseBody(state);
		return node;
	}

	bool DestructuringNode::Ahead(const Lexer& lexer)
	{
		// `[` ... `]` then `=`: an annotation group is followed by a declaration, a lambda by `(`.
		if (lexer.PeekCompound().GetType() != TokenType::OpenBracket) return false;
		int32 depth = 0;
		for (uint32 i = 0; i < kMaxLookahead; ++i)
		{
			const TokenType type = lexer.PeekCompound(i).GetType();
			if (type == TokenType::EndOfFile) return false;
			if (type == TokenType::OpenBracket || type == TokenType::OpenParen || type == TokenType::OpenCurl) ++depth;
			else if ((type == TokenType::CloseBracket || type == TokenType::CloseParen || type == TokenType::CloseCurl) && --depth == 0)
				return lexer.PeekCompound(i + 1).GetType() == TokenType::Equal;
		}
		return false;
	}

	void DestructuringNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		startLocation = lexer.PeekCompound().GetStartLocation();
		lexer.NextCompoundToken(); // [
		while (true)
		{
			m_Elements.Push(ParseElement(state));
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken();
		}
		Expect(state, TokenType::CloseBracket, "']' after the destructured elements");
		Expect(state, TokenType::Equal, "'=' after the destructured elements");
		m_Value = PrattParser::ParseExpression(state);
		endLocation = lexer.PeekCompound().GetEndLocation();
		Expect(state, TokenType::SemiColon, "';' after the destructuring");
		for (const Element& e : m_Elements)
		{
			if (e.type) m_Children.Push(e.type);
			if (e.target) m_Children.Push(e.target);
		}
		if (m_Value) m_Children.Push(m_Value);
	}

	DestructuringNode::Element DestructuringNode::ParseElement(ParserState& state)
	{
		// `_` discards; `Type name` declares; anything else is a location to assign.
		Lexer& lexer = state.GetLexer();
		Element element;
		const Token first = lexer.PeekCompound();
		if (first.GetType() == TokenType::Identifier && first.GetText() == kDiscard && EndsElement(lexer.PeekCompound(1).GetType()))
		{
			lexer.NextCompoundToken();
			element.name = first;
			return element;
		}
		{
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();
			auto type = QualTypeNode::TryParse(state);
			const Token name = lexer.PeekCompound();
			if (type && name.GetType() == TokenType::Identifier && EndsElement(lexer.PeekCompound(1).GetType()))
			{
				lexer.NextCompoundToken();
				state.GetErrorTracker().CommitSuppressedErrors();
				undo.Commit();
				element.kind = Element::Kind::Declare;
				element.type = type;
				element.name = name;
				return element;
			}
			state.GetErrorTracker().DiscardSuppressedErrors();
		}
		element.kind = Element::Kind::Assign;
		element.target = PrattParser::ParseExpression(state);
		return element;
	}

	Core::String DestructuringNode::ToString() const
	{
		Core::String result = "[";
		for (uint32 i = 0; i < m_Elements.Count(); ++i)
		{
			const Element& e = m_Elements[i];
			result += i ? ", " : "";
			if (e.kind == Element::Kind::Declare) result += e.type->ToString() + " " + e.name.GetText();
			else if (e.kind == Element::Kind::Assign) result += e.target ? e.target->ToString() : Core::String();
			else result += kDiscard;
		}
		return result + "] = " + (m_Value ? m_Value->ToString() : Core::String()) + ";";
	}
} // namespace Scripting
