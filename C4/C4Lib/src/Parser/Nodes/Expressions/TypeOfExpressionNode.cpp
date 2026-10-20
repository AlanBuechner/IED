#include "TypeOfExpressionNode.h"
#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"

namespace Scripting
{
	namespace
	{
		TypeOfExpressionNode::Kind KindOf(const Core::String& text)
		{
			using Kind = TypeOfExpressionNode::Kind;
			if (text == "alignof") return Kind::AlignOf;
			if (text == "offsetof") return Kind::OffsetOf;
			if (text == "__uuidof") return Kind::UuidOf;
			if (text == "nameof") return Kind::NameOf;
			return Kind::SizeOf;
		}

		// `(expr)` read ahead and rolled back: the node survives, the lexer does not move.
		Ref<AstNode> SpeculativeParenthesizedExpression(ParserState& state)
		{
			Lexer& lexer = state.GetLexer();
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();
			lexer.NextCompoundToken(); // (
			Ref<AstNode> expression = PrattParser::ParseExpression(state, 0);
			const bool parsed = !state.GetErrorTracker().SpeculationFailed() && lexer.PeekCompound().GetType() == TokenType::CloseParen;
			state.GetErrorTracker().DiscardSuppressedErrors();
			undo.Cancel();
			return parsed ? expression : nullptr;
		}
	}

	bool TypeOfExpressionNode::IsKeyword(const Core::String& text)
	{
		return text == "sizeof" || text == "alignof" || text == "offsetof" || text == "__uuidof" || text == "nameof";
	}

	Ref<TypeOfExpressionNode> TypeOfExpressionNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token keyword = lexer.PeekCompound();
		if (keyword.GetType() != TokenType::Identifier || !IsKeyword(keyword.GetText()))
			return nullptr;

		auto node = CreateRef<TypeOfExpressionNode>();
		node->m_KeywordToken = keyword;
		node->m_Kind = KindOf(keyword.GetText());
		node->startLocation = keyword.GetStartLocation();
		node->endLocation = keyword.GetEndLocation();
		lexer.NextCompoundToken();

		if (node->m_Kind == Kind::SizeOf && lexer.PeekCompound().GetType() == TokenType::Ellipsis)
			node->ParsePack(state);
		else if (!node->ParseTypeOperand(state) && !node->ParseExpressionOperand(state))
		{
			const Token bad = lexer.PeekCompound();
			const char* expected = node->m_Kind == Kind::OffsetOf ? "'(Type, field)'" : "'(Type)'";
			state.GetErrorTracker().AddError(bad.GetStartLocation(),
				"Expected " + Core::String(expected) + " after '" + keyword.GetText() + "', got '" + bad.GetText() + "'", ErrorSeverity::Error);
		}
		return node;
	}

	// `sizeof...(Ts)` / `sizeof...(args)`: the number of elements of a pack (D-19).
	bool TypeOfExpressionNode::ParsePack(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		m_Kind = Kind::SizeOfPack;
		lexer.NextCompoundToken(); // ...
		const Token open = lexer.PeekCompound();
		const Token pack = lexer.PeekCompound(1);
		const Token close = lexer.PeekCompound(2);
		if (open.GetType() != TokenType::OpenParen || pack.GetType() != TokenType::Identifier || close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(open.GetStartLocation(), "Expected '(pack)' after 'sizeof...'", ErrorSeverity::Error);
			return false;
		}
		lexer.NextCompoundToken();
		lexer.NextCompoundToken();
		lexer.NextCompoundToken();
		m_PackName = pack;
		endLocation = close.GetEndLocation();
		return true;
	}

	// `(Type)`, or `(Type, field)` for offsetof. The same tokens are also kept as an expression when they read
	// as one (`sizeof(x)`, `sizeof(a[0])`): the model takes the type when it names one, else the expression.
	bool TypeOfExpressionNode::ParseTypeOperand(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() != TokenType::OpenParen)
			return false;
		const bool takesExpression = m_Kind == Kind::SizeOf || m_Kind == Kind::AlignOf || m_Kind == Kind::NameOf;
		Ref<AstNode> expression = takesExpression ? SpeculativeParenthesizedExpression(state) : nullptr;

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();
		lexer.NextCompoundToken(); // (
		auto type = QualTypeNode::TryParse(state);
		const TokenType after = lexer.PeekCompound().GetType();
		const bool fits = type && after == (m_Kind == Kind::OffsetOf ? TokenType::Comma : TokenType::CloseParen);
		if (!fits)
		{
			state.GetErrorTracker().DiscardSuppressedErrors();
			undo.Cancel();
			return false;
		}
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		m_TargetType = type;
		m_Operand = expression;
		if (m_Kind == Kind::OffsetOf)
			return ParseFieldPath(state);
		endLocation = lexer.NextCompoundToken().GetEndLocation(); // )
		return true;
	}

	// `, field` or `, field.field` and the closing ')'.
	bool TypeOfExpressionNode::ParseFieldPath(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		lexer.NextCompoundToken(); // ,
		while (true)
		{
			const Token name = lexer.PeekCompound();
			if (name.GetType() != TokenType::Identifier)
			{
				state.GetErrorTracker().AddError(name.GetStartLocation(), "offsetof needs a field name, got '" + name.GetText() + "'", ErrorSeverity::Error);
				return true;
			}
			lexer.NextCompoundToken();
			m_FieldPath.Push(name);
			if (lexer.PeekCompound().GetType() != TokenType::Dot) break;
			lexer.NextCompoundToken();
		}
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(close.GetStartLocation(), "Expected ')' to close offsetof, got '" + close.GetText() + "'", ErrorSeverity::Error);
			return true;
		}
		endLocation = lexer.NextCompoundToken().GetEndLocation();
		return true;
	}

	// `sizeof expr` / `sizeof(a + b)`: the operand binds like a prefix operator's (`sizeof x * 2` is `(sizeof x) * 2`).
	bool TypeOfExpressionNode::ParseExpressionOperand(ParserState& state)
	{
		if (m_Kind != Kind::SizeOf && m_Kind != Kind::AlignOf && m_Kind != Kind::NameOf)
			return false;
		m_Operand = PrattParser::ParseExpression(state, BindPower::PrefixOperand);
		if (!m_Operand)
			return false;
		endLocation = m_Operand->endLocation;
		return true;
	}

	std::span<const Ref<AstNode>> TypeOfExpressionNode::Children() const
	{
		if (m_Children.Count() == 0)
		{
			if (m_TargetType) m_Children.Push(m_TargetType);
			else if (m_Operand) m_Children.Push(m_Operand);
		}
		return m_Children;
	}

	Core::String TypeOfExpressionNode::ToString() const
	{
		if (m_Kind == Kind::SizeOfPack) return "sizeof...(" + m_PackName.GetText() + ")";
		Core::String text = m_KeywordToken.GetText() + "(" + (m_TargetType ? m_TargetType->ToString() : m_Operand ? m_Operand->ToString() : "?");
		for (uint32 i = 0; i < m_FieldPath.Count(); ++i)
			text += (i == 0 ? ", " : ".") + m_FieldPath[i].GetText();
		return text + ")";
	}
}
