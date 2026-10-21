#include "Parser/PrattParser.h"
#include "Lexer/Lexer.h"
#include "Core/StringFormat.h"
#include "Parser/UnexpectedToken.h"
#include "Nodes/Expressions/BinaryExpressionNode.h"
#include "Nodes/Expressions/UnaryExpressionNode.h"
#include "Nodes/Expressions/TernaryExpressionNode.h"
#include "Nodes/Expressions/FunctionCallNode.h"
#include "Nodes/Expressions/MemberAccessNode.h"
#include "Nodes/Expressions/StaticAccessNode.h"
#include "Nodes/Expressions/ValueListNode.h"
#include "Nodes/Expressions/ArrayIndexNode.h"
#include "Nodes/Expressions/AssignmentExpressionNode.h"
#include "Nodes/Expressions/ConditionalAndNode.h"
#include "Nodes/Expressions/ConditionalOrNode.h"
#include "Nodes/Expressions/TypeCheckNode.h"
#include "Nodes/Expressions/SafeCastNode.h"
#include "Nodes/Expressions/CastExpressionNode.h"
#include "Nodes/Expressions/TypeOfExpressionNode.h"
#include "Nodes/Expressions/VaArgExpressionNode.h"
#include "Nodes/Expressions/ParenthesizedExpressionNode.h"
#include "Nodes/Expressions/LiteralNode.h"
#include "Nodes/Expressions/IdentifierExpressionNode.h"
#include "Nodes/Expressions/BracedInitializerNode.h"
#include "Nodes/Expressions/LambdaNode.h"
#include "Nodes/Expressions/NewExpressionNode.h"
#include "Nodes/Declarations/QualTypeNode.h"
#include "Nodes/Declarations/NamespaceQualifiedNameNode.h"

namespace Scripting
{
	Ref<AstNode> PrattParser::TryMemberTemplateCall(ParserState& state, const Ref<AstNode>& member)
	{
		// As for `Foo<int32>(x)`: the arguments count only when a complete list is followed by '('.
		Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() != TokenType::Less) return nullptr;
		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();
		auto tmplArgs = CreateRef<NamespaceQualifiedNameNode>()->ParseTemplateArguments(state);
		if (!tmplArgs || state.GetErrorTracker().SpeculationFailed() || lexer.PeekCompound().GetType() != TokenType::OpenParen)
		{
			state.GetErrorTracker().DiscardSuppressedErrors();
			return nullptr;
		}
		state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return FinishTemplateCall(state, member, tmplArgs, member->startLocation);
	}

	bool PrattParser::EndsTemplateName(TokenType type)
	{
		switch (type)
		{
		case TokenType::SemiColon: case TokenType::Comma: case TokenType::CloseParen: case TokenType::CloseBracket:
		case TokenType::CloseCurl: case TokenType::Colon: case TokenType::QuestionMark: case TokenType::DoubleEqual:
		case TokenType::ExclamationEquals: case TokenType::DoubleAmpersand: case TokenType::DoublePipe: case TokenType::Pipe:
		case TokenType::Caret:
			return true;
		default:
			return false;
		}
	}

	Ref<AstNode> PrattParser::FinishTemplateCall(ParserState& state, Ref<AstNode> callee, Ref<TemplateArgumentListNode> tmplArgs,
		const SourceLocation& start)
	{
		Lexer& lexer = state.GetLexer();
		auto call = CreateRef<FunctionCallNode>();
		call->m_Callee = callee;
		call->m_TemplateArgs = tmplArgs;
		call->m_OpenParenToken = lexer.PeekCompound();
		call->startLocation = start;
		lexer.NextCompoundToken(); // (
		while (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			auto arg = ParseArgument(state);
			if (!arg) break;
			call->m_Arguments.Push(arg);
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken(); // a trailing comma is allowed
		}
		Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseParen)
		{
			state.GetErrorTracker().AddError(close.GetStartLocation(),
				Core::StringFormat::Format("Expected ')' to close function call, got '{0}'", close.GetText()), ErrorSeverity::Error);
			return call;
		}
		call->m_CloseParenToken = close;
		call->endLocation = close.GetEndLocation();
		lexer.NextCompoundToken();
		return call;
	}

	Ref<AstNode> PrattParser::ParsePrefix(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token token = lexer.PeekCompound();
		TokenType type = token.GetType();

		// --- Lambda: [](params) -> Ret { body } / [&](...) ---
		if (type == TokenType::OpenBracket)
		{
			if (auto lambda = LambdaNode::TryParse(state))
			{
				lambda->ParseBody(state);
				return lambda;
			}
			return ValueListNode::Parse(state); // `[a, b]`: values returned or destructured together (D-34)
		}

		// --- Braced initializer: {expr, expr, ...} ---
		if (type == TokenType::OpenCurl)
		{
			auto node = CreateRef<BracedInitializerNode>();
			node->ParseBody(state);
			return node;
		}

		// --- Heap allocation: new T(args) / new T{args} ---
		if (type == TokenType::New)
		{
			auto node = CreateRef<NewExpressionNode>();
			node->ParseBody(state);
			return node;
		}

		// --- Unary prefix operators: -, !, ~, ++, --, &, *, + ---
		if (type == TokenType::Minus || type == TokenType::Exclamation ||
			type == TokenType::Tilde || type == TokenType::DoublePlus ||
			type == TokenType::DoubleMinus || type == TokenType::Ampersand ||
			type == TokenType::Star || type == TokenType::Plus)
		{
			// Check if * or & could be part of a C++ cast (e.g. static_cast<int>(x))
			// These are only unary prefix, not casts - casts are handled by identifier path below

			auto node = CreateRef<UnaryExpressionNode>();
			node->m_OperatorToken = token;
			node->m_IsPostfix = false;
			node->startLocation = token.GetStartLocation();
			lexer.NextCompoundToken(); // consume operator

			auto operand = ParseExpression(state, BindPower::PrefixOperand);
			if (!operand)
			{
				state.GetErrorTracker().AddError(
					token.GetStartLocation(),
					Core::StringFormat::Format("Expected operand after '{0}'", token.GetText()),
					ErrorSeverity::Error);
				return node;
			}

			node->m_Operand = operand;
			node->endLocation = operand->endLocation;
			return node;
		}

		// --- `**p`: the lexer merges the two stars (`a ** b` is the power operator), so as a prefix it is two
		// dereferences ---
		if (type == TokenType::DoubleStar)
		{
			lexer.NextCompoundToken();
			auto operand = ParseExpression(state, BindPower::PrefixOperand);
			if (!operand)
			{
				state.GetErrorTracker().AddError(token.GetStartLocation(), "Expected operand after '**'", ErrorSeverity::Error);
				return nullptr;
			}
			Token star = token;
			star.SetType(TokenType::Star);
			star.SetText("*");
			Ref<AstNode> result = operand;
			for (int layer = 0; layer < 2; ++layer)
			{
				auto node = CreateRef<UnaryExpressionNode>();
				node->m_OperatorToken = star;
				node->m_IsPostfix = false;
				node->m_Operand = result;
				node->startLocation = token.GetStartLocation();
				node->endLocation = operand->endLocation;
				result = node;
			}
			return result;
		}

		// --- Parenthesized expression or C-style cast ---
		if (type == TokenType::OpenParen)
		{
			return ParseParenOrCStyleCast(state);
		}

		// --- C++-style casts: static_cast<T>(expr), dynamic_cast<T>(expr), etc. ---
		// Also handles identifiers that might start these constructs
		if (type == TokenType::Identifier)
		{
			const Core::String& text = token.GetText();

			// Check for C++ cast keywords
			if (text == "static_cast" || text == "dynamic_cast" ||
				text == "reinterpret_cast" || text == "const_cast")
			{
				return ParseCppCast(state, token);
			}

			// --- Compile-time queries: sizeof, alignof, offsetof, __uuidof, sizeof... ---
			if (TypeOfExpressionNode::IsKeyword(text))
				return TypeOfExpressionNode::TryParse(state);
			if (VaArgExpressionNode::StartsHere(state))
				return VaArgExpressionNode::TryParse(state);
		}

		// --- Primary expressions: literals, identifiers, and keyword primaries ---
		auto literal = LiteralNode::TryParse(state);
		if (literal) return literal;

		// Explicit function-template call: `Foo<int>(args)`. An identifier followed by '<' is
		// ambiguous with a comparison (`x < y`), so parse the template args speculatively and
		// only commit when '(' follows - otherwise roll back and fall through to the normal
		// identifier path (which yields `x`, then ParseInfix handles `<`).
		if (type == TokenType::Identifier && lexer.PeekCompound(1).GetType() == TokenType::Less)
		{
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();

			Token identToken = lexer.PeekCompound();
			lexer.NextCompoundToken(); // consume identifier

			auto argsNode = CreateRef<NamespaceQualifiedNameNode>();
			auto tmplArgs = argsNode->ParseTemplateArguments(state);

			// Only a complete argument list followed by '(' is a call: `i < Make(x).v` is a comparison.
			if (tmplArgs && !state.GetErrorTracker().SpeculationFailed() && lexer.PeekCompound().GetType() == TokenType::OpenParen)
			{
				state.GetErrorTracker().CommitSuppressedErrors();
				undo.Commit();
				auto identNode = CreateRef<IdentifierExpressionNode>();
				identNode->SetNameToken(identToken);
				return FinishTemplateCall(state, identNode, tmplArgs, identToken.GetStartLocation());
			}

			if (tmplArgs)
			{
				Token after = lexer.PeekCompound(); // index 0 - safe, no read-ahead side effect
				const bool instanceValue = EndsTemplateName(after.GetType()) && !state.GetErrorTracker().SpeculationFailed();
				if (after.GetType() == TokenType::OpenCurl || after.GetType() == TokenType::DoubleColon || instanceValue)
				{
					// `Type<args>{...}` composite literal or `Type<args>::member` static access: the
					// instantiation is a TYPE used as an expression prefix. Commit it as a qualified
					// name node; ParsePostfix chains the '{' / '::' onto it, and semantic resolves it
					// through the type query (instantiating the template on demand). `F<args>;` is a function
					// template's instance as a value (BUG-264). A real comparison (`a < b`) never reaches
					// here: its '>' is consumed as the closer but what follows is an operand.
					state.GetErrorTracker().CommitSuppressedErrors();
					undo.Commit();
					return NamespaceQualifiedNameNode::FromParts(identToken, tmplArgs);
				}
			}

			state.GetErrorTracker().DiscardSuppressedErrors();
			// Not a template call - undo rolled the lexer back to the identifier.
		}

		// --- Qualified instantiation prefix: `Math::Vec2<float>` as an expression base. A bare
		// identifier followed by '<' is handled above; here the name itself is namespace-qualified
		// (ident :: ident<...>), which type position already accepts but expression position used to
		// reject with a parse error. Speculatively collect the leading qualified run and commit ONLY
		// when it actually names an instantiation (some segment carries template args) - a plain
		// `A::B` chain has none, so it falls through to the normal identifier + ParsePostfix
		// static-access path unchanged. A `::member` chain after an instantiated segment is left in
		// the stream for ParsePostfix to re-chain onto (Semantic/HIR already handle a StaticAccess
		// whose base is a qualified name).
		if (type == TokenType::Identifier && lexer.PeekCompound(1).GetType() == TokenType::DoubleColon)
		{
			auto undo = LexerUndoScope(lexer);
			state.GetErrorTracker().SuppressErrors();

			Core::Vector<NamespaceQualifiedNameNode::Segment> segments;
			bool hasTemplateArgs = false;
			while (true)
			{
				Token nameTok = lexer.PeekCompound();
				if (nameTok.GetType() != TokenType::Identifier) break;
				lexer.NextCompoundToken(); // consume identifier

				NamespaceQualifiedNameNode::Segment seg;
				seg.nameToken = nameTok;
				if (lexer.PeekCompound().GetType() == TokenType::Less)
				{
					auto tmplArgs = CreateRef<NamespaceQualifiedNameNode>()->ParseTemplateArguments(state);
					seg.templateArgs = tmplArgs;
					hasTemplateArgs = true;
				}
				segments.Push(seg);

				Token nextTok = lexer.PeekCompound();
				if (nextTok.GetType() != TokenType::DoubleColon) break; // no chain after this segment
				if (seg.templateArgs) break; // `::member` after an instantiation is not a type segment
				lexer.NextCompoundToken(); // consume '::' and continue collecting namespace segments
			}

			if (hasTemplateArgs && !state.GetErrorTracker().SpeculationFailed())
			{
				Token after = lexer.PeekCompound(); // index 0 - safe, no read-ahead side effect
				const bool instanceValue = EndsTemplateName(after.GetType()) && segments.Back().templateArgs; // `Ns::F<args>;`
				if (after.GetType() == TokenType::OpenCurl || after.GetType() == TokenType::DoubleColon || instanceValue)
				{
					state.GetErrorTracker().CommitSuppressedErrors();
					undo.Commit();
					return NamespaceQualifiedNameNode::FromSegments(segments);
				}
				// `Ns::F<args>(...)`: a call of a namespaced function template. The callee is the qualified name
				// without the arguments, which the call carries.
				if (after.GetType() == TokenType::OpenParen && segments.Back().templateArgs)
				{
					state.GetErrorTracker().CommitSuppressedErrors();
					undo.Commit();
					auto tmplArgs = segments.Back().templateArgs;
					segments.Back().templateArgs = nullptr;
					const SourceLocation start = segments[0].nameToken.GetStartLocation();
					return FinishTemplateCall(state, NamespaceQualifiedNameNode::FromSegments(segments), tmplArgs, start);
				}
			}

			state.GetErrorTracker().DiscardSuppressedErrors();
			// Not an instantiation prefix - undo rolled the lexer back to the identifier.
		}

		auto identifier = IdentifierExpressionNode::TryParse(state);
		if (identifier) return identifier;

		// --- Keyword primaries: delete, this, null (tokens that act as expression prefixes) ---
		if (type == TokenType::This || type == TokenType::Null)
		{
			auto node = CreateRef<IdentifierExpressionNode>();
			node->SetNameToken(token);
			node->startLocation = token.GetStartLocation();
			node->endLocation = token.GetEndLocation();
			lexer.NextCompoundToken(); // consume the keyword
			return node;
		}

		// --- delete and new: unary prefix operators ---
		if (type == TokenType::Delete || type == TokenType::New)
		{
			auto node = CreateRef<UnaryExpressionNode>();
			node->m_OperatorToken = token;
			node->m_IsPostfix = false;
			node->startLocation = token.GetStartLocation();
			lexer.NextCompoundToken(); // consume delete/new
			if (type == TokenType::Delete && lexer.PeekCompound().GetType() == TokenType::OpenBracket
				&& lexer.PeekCompound(1).GetType() == TokenType::CloseBracket)
			{
				node->m_IsArrayDelete = true;
				lexer.NextCompoundToken();
				lexer.NextCompoundToken();
			}

			auto operand = ParseExpression(state, BindPower::PrefixOperand);
			if (!operand)
			{
				state.GetErrorTracker().AddError(
					token.GetStartLocation(),
					Core::StringFormat::Format("Expected operand after '{0}'", token.GetText()),
					ErrorSeverity::Error);
				return node;
			}

			node->m_Operand = operand;
			node->endLocation = operand->endLocation;
			return node;
		}

		// --- Nothing matched --- (a keyword here was meant as a name: `return alias + 1;`)
		state.GetErrorTracker().AddError(
			token.GetStartLocation(),
			Token::IsKeyword(token.GetType()) ? KeywordNameMessage(token) : Core::StringFormat::Format("Expected expression, got '{0}'", token.GetText()),
			ErrorSeverity::Error);
		return nullptr;
	}

	// =========================================================================
	// Infix parsing - binary operators, ternary, assignments, type operators
	// =========================================================================

	Ref<QualTypeNode> PrattParser::ParseTargetType(ParserState& state, const Token& keyword)
	{
		state.GetErrorTracker().SuppressErrors();
		auto targetType = QualTypeNode::TryParse(state);
		if (targetType)
		{
			state.GetErrorTracker().CommitSuppressedErrors();
			return targetType;
		}
		state.GetErrorTracker().DiscardSuppressedErrors();
		const Token bad = state.GetLexer().PeekCompound();
		state.GetErrorTracker().AddError(bad.GetStartLocation(),
			Core::StringFormat::Format("Expected type after '{0}', got '{1}'", keyword.GetText(), bad.GetText()),
			ErrorSeverity::Error);
		return nullptr;
	}
} // namespace Scripting
