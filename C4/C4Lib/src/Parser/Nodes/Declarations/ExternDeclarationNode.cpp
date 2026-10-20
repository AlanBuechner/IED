#include "ExternDeclarationNode.h"
#include "Core/StringFormat.h"
#include "Compiler/CompilerMessageQueue.h"

namespace Scripting
{
	Ref<ExternDeclarationNode> ExternDeclarationNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::Extern)
			return Ref<ExternDeclarationNode>();

		lexer.NextCompoundToken(); // consume `extern` - committed to this node

		auto node = CreateRef<ExternDeclarationNode>();
		node->ParseBody(state);
		return node;
	}

	void ExternDeclarationNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		// 1. Language string: "c" or "c++".
		Token langToken = lexer.PeekCompound();
		if (langToken.GetType() != TokenType::Text)
		{
			errorTracker.AddError(
				langToken.GetStartLocation(),
				Core::StringFormat::Format("Expected language string ('\"c\"' or '\"c++\"') after 'extern', got '{0}'", langToken.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken(); // recovery
			return;
		}
		m_Language = langToken.GetText();
		lexer.NextCompoundToken();

		if (m_Language != "c" && m_Language != "c++")
		{
			errorTracker.AddError(
				langToken.GetStartLocation(),
				Core::StringFormat::Format("Unknown extern language '{0}' - expected 'c' or 'c++'", m_Language),
				ErrorSeverity::Error);
		}

		// 2. Kind: include | source.
		Token kindToken = lexer.PeekCompound();
		if (kindToken.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				kindToken.GetStartLocation(),
				Core::StringFormat::Format("Expected 'include' or 'source' after extern language, got '{0}'", kindToken.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken(); // recovery
			return;
		}

		if (kindToken.GetText() == "include")
			m_Kind = Kind::Include;
		else if (kindToken.GetText() == "source")
			m_Kind = Kind::Source;
		else
		{
			errorTracker.AddError(
				kindToken.GetStartLocation(),
				Core::StringFormat::Format("Expected 'include' or 'source' after extern language, got '{0}'", kindToken.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken(); // recovery
			return;
		}
		lexer.NextCompoundToken();

		// 3. Path string literal.
		Token pathToken = lexer.PeekCompound();
		if (pathToken.GetType() != TokenType::Text)
		{
			errorTracker.AddError(
				pathToken.GetStartLocation(),
				Core::StringFormat::Format("Expected string literal for extern path, got '{0}'", pathToken.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken(); // recovery
			return;
		}
		m_PathToken = pathToken;
		lexer.NextCompoundToken();

		endLocation = m_PathToken.GetEndLocation();

		// 4. Optional config block: { define("N", "V"); include_dir("D"); ... }
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::OpenCurl)
		{
			lexer.NextCompoundToken(); // consume '{'

			while (true)
			{
				Token t = lexer.PeekCompound();
				if (t.GetType() == TokenType::EndOfFile || t.GetType() == TokenType::CloseCurl)
					break;

				if (t.GetType() != TokenType::Identifier)
				{
					errorTracker.AddError(
						t.GetStartLocation(),
						Core::StringFormat::Format("Expected 'define' or 'include_dir' in extern config block, got '{0}'", t.GetText()),
						ErrorSeverity::Error);
					lexer.NextCompoundToken(); // recovery
					continue;
				}

				const Core::String& directive = t.GetText();
				lexer.NextCompoundToken(); // consume directive name

				Token openParen = lexer.PeekCompound();
				if (openParen.GetType() != TokenType::OpenParen)
				{
					errorTracker.AddError(
						openParen.GetStartLocation(),
						Core::StringFormat::Format("Expected '(' after '{0}', got '{1}'", directive, openParen.GetText()),
						ErrorSeverity::Error);
					break;
				}
				lexer.NextCompoundToken(); // consume '('

				Token arg1 = lexer.PeekCompound();
				if (arg1.GetType() != TokenType::Text)
				{
					errorTracker.AddError(
						arg1.GetStartLocation(),
						Core::StringFormat::Format("Expected string argument in '{0}(...)', got '{1}'", directive, arg1.GetText()),
						ErrorSeverity::Error);
					break;
				}
				Core::String first = arg1.GetText();
				lexer.NextCompoundToken();

				if (directive == "define")
				{
					Core::String value;
					Token comma = lexer.PeekCompound();
					if (comma.GetType() == TokenType::Comma)
					{
						lexer.NextCompoundToken(); // consume ','
						Token arg2 = lexer.PeekCompound();
						if (arg2.GetType() != TokenType::Text)
						{
							errorTracker.AddError(
								arg2.GetStartLocation(),
								Core::StringFormat::Format("Expected string value in define('{0}', ...), got '{1}'", first, arg2.GetText()),
								ErrorSeverity::Error);
						}
						else
						{
							value = arg2.GetText();
							lexer.NextCompoundToken(); // consume value
						}
					}

					Define def;
					def.name = first;
					def.value = value;
					m_Defines.Push(def);
				}
				else if (directive == "include_dir")
				{
					m_IncludeDirs.Push(first);
				}
				else if (directive == "lib")
				{
					m_Libraries.Push(first);
				}
				else
				{
					errorTracker.AddError(
						t.GetStartLocation(),
						Core::StringFormat::Format("Unknown extern config directive '{0}'", directive),
						ErrorSeverity::Error);
				}

				Token closeParen = lexer.PeekCompound();
				if (closeParen.GetType() != TokenType::CloseParen)
				{
					errorTracker.AddError(
						closeParen.GetStartLocation(),
						Core::StringFormat::Format("Expected ')' to close '{0}(...)', got '{1}'", directive, closeParen.GetText()),
						ErrorSeverity::Error);
					break;
				}
				lexer.NextCompoundToken(); // consume ')'

				Token semi = lexer.PeekCompound();
				if (semi.GetType() == TokenType::SemiColon)
					lexer.NextCompoundToken(); // consume ';'
			}

			Token closeBrace = lexer.PeekCompound();
			if (closeBrace.GetType() == TokenType::CloseCurl)
			{
				endLocation = closeBrace.GetEndLocation();
				lexer.NextCompoundToken(); // consume '}'
			}
			else
			{
				errorTracker.AddError(
					closeBrace.GetStartLocation(),
					Core::StringFormat::Format("Expected '}' to close extern config block, got '{0}'", closeBrace.GetText()),
					ErrorSeverity::Error);
			}

			next = lexer.PeekCompound();
		}

		// 5. Optional trailing semicolon.
		if (next.GetType() == TokenType::SemiColon)
		{
			endLocation = next.GetEndLocation();
			lexer.NextCompoundToken(); // consume ';'
		}
	}

	Core::String ExternDeclarationNode::ToString() const
	{
		const char* kindStr = (m_Kind == Kind::Include) ? "include" : "source";
		return Core::StringFormat::Format("extern \"{0}\" {1} \"{2}\"", m_Language, kindStr, FilePath());
	}

} // namespace Scripting
