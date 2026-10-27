#include "LambdaNode.h"
#include "../Declarations/QualTypeNode.h"
#include "../Statements/CodeBlockNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	namespace
	{
		constexpr const char* kMutable = "mutable";
		constexpr uint32 kMaxCaptureListTokens = 256;

		bool Expect(ParserState& state, TokenType type, const char* what)
		{
			Lexer& lexer = state.GetLexer();
			const Token t = lexer.PeekCompound();
			if (t.GetType() == type)
			{
				lexer.NextCompoundToken();
				return true;
			}
			state.GetErrorTracker().AddError(t.GetStartLocation(),
				Core::StringFormat::Format("Expected {0} in lambda, got '{1}'", what, t.GetText()), ErrorSeverity::Error);
			return false;
		}

		bool Accept(Lexer& lexer, TokenType type)
		{
			if (lexer.PeekCompound().GetType() != type) return false;
			lexer.NextCompoundToken();
			return true;
		}
	}

	Ref<LambdaNode> LambdaNode::TryParse(ParserState& state)
	{
		// `[` ... matching `]` then `(`: an annotation list (`[test("a")] void F()`) is never followed by `(`.
		const Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() != TokenType::OpenBracket) return nullptr;
		int32 depth = 0;
		for (uint32 i = 0; i < kMaxCaptureListTokens; ++i)
		{
			switch (lexer.PeekCompound(i).GetType())
			{
			case TokenType::OpenBracket: case TokenType::OpenParen: case TokenType::OpenCurl: ++depth; break;
			case TokenType::CloseParen: case TokenType::CloseCurl: --depth; break;
			case TokenType::CloseBracket:
				if (--depth == 0) return lexer.PeekCompound(i + 1).GetType() == TokenType::OpenParen ? CreateRef<LambdaNode>() : nullptr;
				break;
			case TokenType::EndOfFile: return nullptr;
			default: break;
			}
		}
		return nullptr;
	}

	void LambdaNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		startLocation = lexer.PeekCompound().GetStartLocation();
		if (!ParseCaptureList(state) || !ParseParameters(state)) return;

		const Token maybeMutable = lexer.PeekCompound();
		if (maybeMutable.GetType() == TokenType::Identifier && maybeMutable.GetText() == kMutable)
		{
			m_Mutable = true;
			lexer.NextCompoundToken();
		}
		if (Accept(lexer, TokenType::Arrow))
		{
			m_ReturnType = lexer.PeekCompound().GetType() == TokenType::OpenBracket ? QualTypeNode::ParseReturnList(state) : QualTypeNode::TryParse(state);
			if (!m_ReturnType)
			{
				state.GetErrorTracker().AddError(lexer.PeekCompound().GetStartLocation(),
					"Expected return type after '->' in lambda", ErrorSeverity::Error);
				return;
			}
		}
		const Token open = lexer.PeekCompound();
		if (open.GetType() != TokenType::OpenCurl)
		{
			state.GetErrorTracker().AddError(open.GetStartLocation(),
				Core::StringFormat::Format("Expected an opening brace for the lambda body, got '{0}'", open.GetText()),
				ErrorSeverity::Error);
			return;
		}
		m_Body = CodeBlockNode::TryParse(state);
		if (!m_Body)
		{
			state.GetErrorTracker().AddError(lexer.PeekCompound().GetStartLocation(), "Expected lambda body", ErrorSeverity::Error);
			return;
		}
		endLocation = m_Body->endLocation;
	}

	bool LambdaNode::ParseCaptureList(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		if (!Expect(state, TokenType::OpenBracket, "'['")) return false;
		if (Accept(lexer, TokenType::CloseBracket)) return true;
		do
		{
			if (!ParseCapture(state)) return false;
		} while (Accept(lexer, TokenType::Comma));
		return Expect(state, TokenType::CloseBracket, "']' after the capture list");
	}

	bool LambdaNode::ParseCapture(ParserState& state)
	{
		// `=`, `&` (defaults, first only), `x`, `&x`, `n = expr`, `&r = expr`, `this`, `*this`.
		Lexer& lexer = state.GetLexer();
		const Token first = lexer.PeekCompound();
		const TokenType next = lexer.PeekCompound(1).GetType();
		const bool atStart = m_Captures.Empty() && m_CaptureDefault == CaptureDefault::None;
		if (atStart && first.GetType() == TokenType::Equal && (next == TokenType::Comma || next == TokenType::CloseBracket))
		{
			lexer.NextCompoundToken();
			m_CaptureDefault = CaptureDefault::ByValue;
			return true;
		}
		if (atStart && first.GetType() == TokenType::Ampersand && (next == TokenType::Comma || next == TokenType::CloseBracket))
		{
			lexer.NextCompoundToken();
			m_CaptureDefault = CaptureDefault::ByReference;
			return true;
		}
		LambdaCapture capture;
		if (Accept(lexer, TokenType::This))
		{
			capture.kind = LambdaCapture::Kind::This;
			m_Captures.Push(capture);
			return true;
		}
		if (first.GetType() == TokenType::Star && next == TokenType::This)
		{
			lexer.NextCompoundToken();
			lexer.NextCompoundToken();
			capture.kind = LambdaCapture::Kind::ThisCopy;
			m_Captures.Push(capture);
			return true;
		}
		capture.byReference = Accept(lexer, TokenType::Ampersand);
		capture.name = lexer.PeekCompound();
		if (!Expect(state, TokenType::Identifier, "a capture")) return false;
		const Token assignment = lexer.PeekCompound();
		if (Accept(lexer, TokenType::Equal))
		{
			capture.kind = LambdaCapture::Kind::Init;
			capture.init = PrattParser::ParseExpression(state);
			if (!capture.init) return false;
			capture.declaration = VariableDefinitionNode::ForInitCapture(capture.name, assignment, capture.init);
			m_Children.Push(capture.init);
		}
		m_Captures.Push(capture);
		return true;
	}

	bool LambdaNode::ParseParameters(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		if (!Expect(state, TokenType::OpenParen, "'(' after the capture list")) return false;
		ParameterNode::SkipVoidList(lexer);
		while (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			const Token t = lexer.PeekCompound();
			auto param = ParameterNode::TryParse(state);
			if (!param)
			{
				state.GetErrorTracker().AddError(t.GetStartLocation(),
					Core::StringFormat::Format("Expected lambda parameter, got '{0}'", t.GetText()), ErrorSeverity::Error);
				return false;
			}
			m_Parameters.Push(param);
			if (!Accept(lexer, TokenType::Comma)) break;
		}
		return Expect(state, TokenType::CloseParen, "')' to close the lambda parameters");
	}

	Core::String LambdaNode::ToString() const
	{
		Core::String result = "[";
		bool first = true;
		auto separate = [&]() { if (!first) result += ", "; first = false; };
		if (m_CaptureDefault != CaptureDefault::None) { separate(); result += m_CaptureDefault == CaptureDefault::ByValue ? "=" : "&"; }
		for (const LambdaCapture& c : m_Captures)
		{
			separate();
			if (c.kind == LambdaCapture::Kind::This) { result += "this"; continue; }
			if (c.kind == LambdaCapture::Kind::ThisCopy) { result += "*this"; continue; }
			if (c.byReference) result += "&";
			result += c.name.GetText();
			if (c.init) result += " = " + c.init->ToString();
		}
		result += "](";
		for (uint32 i = 0; i < m_Parameters.Count(); ++i)
		{
			if (i > 0) result += ", ";
			result += m_Parameters[i]->ToString();
		}
		result += ")";
		if (m_Mutable) result += " mutable";
		if (m_ReturnType) result += " -> " + m_ReturnType->ToString();
		result += " { ... }";
		return result;
	}
} // namespace Scripting
