#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents an include declaration in C4 source.
	///
	/// Example: `include "Some/Module.ch"`
	///   FilePath() = "Some/Module.ch"
	///
	/// The file path must end with the `.ch` extension; validation occurs during parsing.
	struct IncludeDeclarationNode : public AstNode
	{
		IncludeDeclarationNode() = default;

		// --- Getters ---

		const Core::String& FilePath() const { return m_FilePathToken.GetText(); }
		const Token& GetFilePathToken() const { return m_FilePathToken; }

		/// Factory: peeks for the `include` keyword, creates a Ref<IncludeDeclarationNode>,
		/// and calls member ParseBody. Returns an empty Ref on failure without consuming
		/// any tokens (keyword match is irrevocable once seen).
		static Ref<IncludeDeclarationNode> TryParse(ParserState& state);

		/// Member: called after the `include` keyword has been consumed. Parses the file
		/// path string literal and optional semicolon into this node. Validates that the
		/// path ends with `.ch`. On internal failure the lexer is recovered to a clean
		/// boundary so subsequent nodes can parse cleanly; the node is still returned
		/// (with whatever fields were successfully filled).
		void ParseBody(ParserState& state);

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::IncludeDeclaration; }

	private:
		Token m_FilePathToken; // included file path token (e.g. "Some/Module.ch")
	};
}
