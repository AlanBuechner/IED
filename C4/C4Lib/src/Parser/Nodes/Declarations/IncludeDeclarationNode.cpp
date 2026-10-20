#include "IncludeDeclarationNode.h"
#include "Core/StringFormat.h"
#include "Compiler/CompilerMessageQueue.h"

namespace Scripting
{

	Ref<IncludeDeclarationNode> IncludeDeclarationNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token token = lexer.PeekCompound();
		if (token.GetType() != TokenType::Identifier || token.GetText() != "include")
			return Ref<IncludeDeclarationNode>();

		lexer.NextCompoundToken(); // consume `include` keyword - we've committed to this node

		auto node = CreateRef<IncludeDeclarationNode>();
		node->ParseBody(state);
		return node;
	}

	void IncludeDeclarationNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		// Expected: "file.ch" [;]

		// 1. File path must be a string literal (TokenType::Text)
		Token pathToken = lexer.PeekCompound();
		if (pathToken.GetType() != TokenType::Text)
		{
			errorTracker.AddError(
				pathToken.GetStartLocation(),
				Core::StringFormat::Format("Expected string literal for include path after 'include', got '{0}'", pathToken.GetText()),
				ErrorSeverity::Error);
			lexer.NextCompoundToken(); // consume this token as recovery
			return; // node still returned with empty filePath
		}

		m_FilePathToken = pathToken;
		lexer.NextCompoundToken(); // consume the path string

		if (m_FilePathToken.GetText().size() < 3 || m_FilePathToken.GetText().substr(m_FilePathToken.GetText().size() - 3) != ".ch")
		{
			errorTracker.AddError(
				pathToken.GetStartLocation(),
				Core::StringFormat::Format("Include file path must have '.ch' extension, got '{0}'", m_FilePathToken.GetText()),
				ErrorSeverity::Error);
		}

		// Request the included file from the compiler instance and wait for resolution.
		CompilerMessageQueue* messageQueue = state.GetMessageQueue();
		if (messageQueue)
		{
			uint32 sourceId = lexer.GetSourceId();
			CompilerResponse response = messageQueue->SendMessageAndAwaitResponse(
				sourceId, MessageType::RequestFileInclude, m_FilePathToken.GetText());

			if (response.type == ResponseType::Error)
			{
				errorTracker.AddError(
					pathToken.GetStartLocation(),
					response.data,
					ErrorSeverity::Error);
			}
		}

		endLocation = lexer.PeekCompound().GetEndLocation();

		// 3. Optional semicolon (consume if present)
		Token next = lexer.PeekCompound();
		if (next.GetType() == TokenType::SemiColon)
		{
			endLocation = next.GetEndLocation();
			lexer.NextCompoundToken(); // consume ';'
		}
	}

	Core::String IncludeDeclarationNode::ToString() const
	{
		return Core::StringFormat::Format("include \"{0}\"", FilePath());
	}

} // namespace Scripting
