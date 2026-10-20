#include "AsmStatementNode.h"
#include "Parser/PrattParser.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	namespace
	{
		bool IsWord(const Token& token, const char* word)
		{
			return token.GetType() == TokenType::Identifier && token.GetText() == word;
		}

		void Error(ParserState& state, const Token& at, const Core::String& message)
		{
			state.GetErrorTracker().AddError(at.GetStartLocation(), message, ErrorSeverity::Error);
		}

		// Skips to the end of a malformed item so the rest of the block still parses.
		void Recover(Lexer& lexer)
		{
			while (true)
			{
				TokenType t = lexer.PeekCompound().GetType();
				if (t == TokenType::EndOfFile || t == TokenType::CloseCurl) return;
				lexer.NextCompoundToken();
				if (t == TokenType::SemiColon) return;
			}
		}
	}

	Core::String AsmStatementNode::ToString() const
	{
		Core::String result = "asm {";
		for (const Core::String& line : m_Lines) result += " \"" + line + "\";";
		return result + " }";
	}

	Ref<AsmStatementNode> AsmStatementNode::TryParse(ParserState& state)
	{
		if (state.GetLexer().PeekCompound().GetType() != TokenType::Asm) return nullptr;
		auto node = CreateRef<AsmStatementNode>();
		node->ParseBody(state);
		return node;
	}

	void AsmStatementNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token keyword = lexer.PeekCompound();
		startLocation = keyword.GetStartLocation();
		endLocation = keyword.GetEndLocation();
		lexer.NextCompoundToken();
		if (lexer.PeekCompound().GetType() == TokenType::OpenParen) ParseOptions(state);
		if (lexer.PeekCompound().GetType() != TokenType::OpenCurl)
		{
			Error(state, lexer.PeekCompound(), "an asm block is written 'asm { \"instruction\"; ... }': expected '{'");
			return;
		}
		lexer.NextCompoundToken();
		while (lexer.PeekCompound().GetType() != TokenType::CloseCurl && lexer.PeekCompound().GetType() != TokenType::EndOfFile)
			ParseItem(state);
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseCurl)
		{
			Error(state, close, "expected '}' to close the asm block");
			return;
		}
		endLocation = close.GetEndLocation();
		lexer.NextCompoundToken();
		if (lexer.PeekCompound().GetType() == TokenType::SemiColon) lexer.NextCompoundToken(); // `asm { ... };`
	}

	void AsmStatementNode::ParseOptions(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		lexer.NextCompoundToken(); // (
		while (lexer.PeekCompound().GetType() == TokenType::Identifier)
		{
			m_Options.Push(lexer.PeekCompound());
			lexer.NextCompoundToken();
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken();
		}
		if (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			Error(state, lexer.PeekCompound(), "the options of an asm block are names separated by ',': expected ')'");
			return;
		}
		lexer.NextCompoundToken();
	}

	void AsmStatementNode::ParseItem(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		const Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::Text) return ParseLine(state);
		if (next.GetType() == TokenType::Const) return ParseConstant(state);
		if (IsWord(next, "in")) return ParseOperand(state, AsmOperand::Kind::In);
		if (IsWord(next, "out")) return ParseOperand(state, AsmOperand::Kind::Out);
		if (IsWord(next, "inout")) return ParseOperand(state, AsmOperand::Kind::InOut);
		if (IsWord(next, "clobber")) return ParseClobbers(state, false);
		if (IsWord(next, "clobber_abi")) return ParseClobbers(state, true);
		if (next.GetType() == TokenType::SemiColon)
		{
			lexer.NextCompoundToken();
			return;
		}
		Error(state, next, "'" + next.GetText() + "' is not part of an asm block: write \"instruction\";, in(...), out(...), "
			"inout(...), const, clobber(...) or clobber_abi(...)");
		Recover(lexer);
	}

	void AsmStatementNode::ParseLine(ParserState& state)
	{
		// Adjacent strings join into one line, as everywhere; the ';' ends it.
		auto literal = LiteralNode::TryParse(state);
		m_Lines.Push(literal ? literal->GetValue() : Core::String());
		ExpectSemicolon(state, "an asm line");
	}

	bool AsmStatementNode::ParseRegisterClass(ParserState& state, AsmOperand& operand)
	{
		Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() != TokenType::OpenParen)
		{
			Error(state, lexer.PeekCompound(), "an asm operand names its register: in(reg) x, in(xmm) v or in(\"eax\") x");
			return false;
		}
		lexer.NextCompoundToken();
		const Token name = lexer.PeekCompound();
		if (name.GetType() == TokenType::Identifier) operand.registerClass = name.GetText();
		else if (name.GetType() == TokenType::Text)
		{
			auto literal = LiteralNode::TryParse(state);
			operand.registerClass = literal ? literal->GetValue() : Core::String();
			operand.explicitRegister = true;
		}
		else
		{
			Error(state, name, "expected a register class (reg, xmm, ymm, zmm) or a register name in quotes");
			return false;
		}
		if (!operand.explicitRegister) lexer.NextCompoundToken();
		if (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			Error(state, lexer.PeekCompound(), "expected ')' after the register of an asm operand");
			return false;
		}
		lexer.NextCompoundToken();
		return true;
	}

	void AsmStatementNode::ParseOperand(ParserState& state, AsmOperand::Kind kind)
	{
		Lexer& lexer = state.GetLexer();
		AsmOperand operand;
		operand.kind = kind;
		operand.location = lexer.PeekCompound().GetStartLocation();
		lexer.NextCompoundToken(); // in / out / inout
		if (!ParseRegisterClass(state, operand)) return Recover(lexer);
		// `name = expr` names an expression; a bare variable is named by itself; anything else is positional.
		if (lexer.PeekCompound().GetType() == TokenType::Identifier && lexer.PeekCompound(1).GetType() == TokenType::Equal)
		{
			operand.name = lexer.PeekCompound().GetText();
			lexer.NextCompoundToken();
			lexer.NextCompoundToken();
		}
		else if (lexer.PeekCompound().GetType() == TokenType::Identifier && lexer.PeekCompound(1).GetType() == TokenType::SemiColon)
			operand.name = lexer.PeekCompound().GetText();
		operand.value = PrattParser::ParseExpression(state);
		if (!operand.value)
		{
			Error(state, lexer.PeekCompound(), "expected the value or location of the asm operand");
			return Recover(lexer);
		}
		m_Children.Push(operand.value);
		m_Operands.Push(operand);
		ExpectSemicolon(state, "an asm operand");
	}

	void AsmStatementNode::ParseConstant(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		AsmOperand operand;
		operand.kind = AsmOperand::Kind::Const;
		operand.location = lexer.PeekCompound().GetStartLocation();
		lexer.NextCompoundToken(); // const
		if (lexer.PeekCompound().GetType() != TokenType::Identifier || lexer.PeekCompound(1).GetType() != TokenType::Equal)
		{
			Error(state, lexer.PeekCompound(), "an asm constant is written 'const name = value;'");
			return Recover(lexer);
		}
		operand.name = lexer.PeekCompound().GetText();
		lexer.NextCompoundToken();
		lexer.NextCompoundToken();
		operand.value = PrattParser::ParseExpression(state);
		if (!operand.value)
		{
			Error(state, lexer.PeekCompound(), "expected the value of the asm constant '" + operand.name + "'");
			return Recover(lexer);
		}
		m_Children.Push(operand.value);
		m_Operands.Push(operand);
		ExpectSemicolon(state, "an asm constant");
	}

	void AsmStatementNode::ParseClobbers(ParserState& state, bool abi)
	{
		Lexer& lexer = state.GetLexer();
		const Token word = lexer.PeekCompound();
		lexer.NextCompoundToken();
		if (lexer.PeekCompound().GetType() != TokenType::OpenParen)
		{
			Error(state, lexer.PeekCompound(), "'" + word.GetText() + "' takes register names in quotes: clobber(\"rcx\", \"memory\")");
			return Recover(lexer);
		}
		lexer.NextCompoundToken();
		while (lexer.PeekCompound().GetType() == TokenType::Text)
		{
			auto literal = LiteralNode::TryParse(state);
			const Core::String value = literal ? literal->GetValue() : Core::String();
			if (abi)
			{
				if (value != "C") Error(state, word, "clobber_abi names the calling convention whose volatile registers change: only \"C\"");
				m_ClobbersCallerSaved = true;
			}
			else m_Clobbers.Push(value);
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken();
		}
		if (lexer.PeekCompound().GetType() != TokenType::CloseParen)
		{
			Error(state, lexer.PeekCompound(), "expected ')' to close '" + word.GetText() + "'");
			return Recover(lexer);
		}
		lexer.NextCompoundToken();
		ExpectSemicolon(state, "a clobber list");
	}

	bool AsmStatementNode::ExpectSemicolon(ParserState& state, const char* after)
	{
		Lexer& lexer = state.GetLexer();
		if (lexer.PeekCompound().GetType() == TokenType::SemiColon)
		{
			lexer.NextCompoundToken();
			return true;
		}
		if (lexer.PeekCompound().GetType() == TokenType::CloseCurl) return true; // the last item may omit it
		Error(state, lexer.PeekCompound(), Core::StringFormat::Format("expected ';' after {0}", after));
		Recover(lexer);
		return false;
	}
}
