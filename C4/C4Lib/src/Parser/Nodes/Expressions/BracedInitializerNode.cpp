#include "BracedInitializerNode.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	std::span<const Ref<AstNode>> BracedInitializerNode::Children() const
	{
		return m_Elements;
	}

	Core::String BracedInitializerNode::ToString() const
	{
		// The entries in source order, as written: `{ 1, [5] = 9, [6...8] = 0, y = 2 }`.
		Core::String result = "{";
		for (uint32 i = 0; i < m_Entries.Count(); ++i)
		{
			const Entry& e = m_Entries[i];
			result += i == 0 ? " " : ", ";
			if (e.m_IsRange)
				result += "[" + e.m_Key->ToString() + "..." + (e.m_RangeEnd ? e.m_RangeEnd->ToString() : "") + "] = ";
			else if (e.m_Key && e.m_IsBracketed)
				result += "[" + e.m_Key->ToString() + "] = ";
			else if (e.m_Key)
				result += e.m_Key->ToString() + " = ";
			result += e.m_Value ? e.m_Value->ToString() : Core::String();
		}
		return result + (m_Entries.Empty() ? "}" : " }");
	}

	void BracedInitializerNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		// Consume '{'
		Token openBrace = lexer.PeekCompound();
		m_OpenBraceToken = openBrace;
		startLocation = openBrace.GetStartLocation();
		lexer.NextCompoundToken();

		// Parse comma-separated entries until '}'. Each entry is either a plain positional
		// expression, or an initializer-map entry: [key] = value, key = value, or [a...b] = value.
		Token next = lexer.PeekCompound();
		if (next.GetType() != TokenType::CloseCurl)
		{
			ParseOneEntry(state);

			while (true)
			{
				next = lexer.PeekCompound();
				if (next.GetType() == TokenType::Comma)
				{
					lexer.NextCompoundToken();
					next = lexer.PeekCompound();
					if (next.GetType() != TokenType::CloseCurl)
						ParseOneEntry(state);
				}
				else
					break;
			}
		}

		// Consume '}'
		next = lexer.PeekCompound();
		if (next.GetType() == TokenType::CloseCurl)
		{
			m_CloseBraceToken = next;
			endLocation = next.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected '}' to close braced initializer, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			endLocation = startLocation;
		}
	}

	void BracedInitializerNode::ParseOneEntry(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();
		Token t = lexer.PeekCompound();

		if (t.GetType() == TokenType::OpenBracket)
		{
			// Bracketed key: [key] or [start...end]. The lexer splits `2...5` into 2 / ... / 5, so
			// a range is simply an expression followed by '...' (or '..').
			lexer.NextCompoundToken(); // consume '['
			auto key = PrattParser::ParseExpression(state);
			Token afterKey = lexer.PeekCompound();
			bool isRange = false;
			Ref<AstNode> rangeEnd;
			if (afterKey.GetType() == TokenType::Ellipsis || afterKey.GetType() == TokenType::DotDot)
			{
				isRange = true;
				lexer.NextCompoundToken(); // consume '..' / '...'
				rangeEnd = PrattParser::ParseExpression(state);
				afterKey = lexer.PeekCompound();
			}
			if (afterKey.GetType() != TokenType::CloseBracket)
			{
				errorTracker.AddError(afterKey.GetStartLocation(),
					Core::StringFormat::Format("Expected ']' after initializer-map key, got '{0}'", afterKey.GetText()),
					ErrorSeverity::Error);
				return;
			}
			lexer.NextCompoundToken(); // consume ']'
			Token eq = lexer.PeekCompound();
			if (eq.GetType() != TokenType::Equal)
			{
				errorTracker.AddError(eq.GetStartLocation(),
					"Expected '=' after initializer-map key", ErrorSeverity::Error);
				return;
			}
			lexer.NextCompoundToken(); // consume '='
			auto value = PrattParser::ParseExpression(state);

			Entry e;
			e.m_Key = key;
			e.m_IsBracketed = true;
			e.m_IsRange = isRange;
			e.m_RangeEnd = rangeEnd;
			e.m_Value = value;
			m_Entries.Push(e);
			return;
		}

		if (t.GetType() == TokenType::Identifier || t.GetType() == TokenType::Number ||
			t.GetType() == TokenType::Text || t.GetType() == TokenType::Char)
		{
			// Bare key: `key = value` (a map entry). If the token is not followed by '=', it is a
			// plain positional expression instead.
			if (lexer.PeekCompound(1).GetType() == TokenType::Equal)
			{
				auto key = PrattParser::ParseDesignator(state);
				lexer.NextCompoundToken(); // consume '='
				auto value = PrattParser::ParseExpression(state);

				Entry e;
				e.m_Key = key;
				e.m_Value = value;
				m_Entries.Push(e);
				return;
			}
		}

		// Plain positional element. Recorded both as an element (for struct / init-list lowering)
		// and as a keyless entry so the array initializer-map lowering can see source order.
		auto positional = PrattParser::ParseArgument(state);
		m_Elements.Push(positional);
		Entry e;
		e.m_Value = positional; // m_Key stays null, m_IsRange false
		m_Entries.Push(e);
	}


} // namespace Scripting
