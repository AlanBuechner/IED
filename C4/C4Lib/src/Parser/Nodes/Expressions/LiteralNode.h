#pragma once
#include "Parser/AstNode.h"
#include "Parser/ParserState.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	/// Represents a literal value expression in C4 source.
	///
	/// Examples: `42`, `3.14f`, `"hello"`, `'c'`, `true`, `null`
	///
	/// This is a leaf expression node that unifies numeric literals, string
	/// literals, and boolean/null keywords into a single AST node type.
	struct LiteralNode : public AstNode
	{
		static constexpr NodeType kNodeType = NodeType::LiteralExpression;
		enum class LiteralKind
		{
			Number,   // TokenType::Number: an integer or floating-point spelling (Model/Literals.h types it)
			String,   // TokenType::Text: the code units of one or more adjacent string literals
			Char,     // TokenType::Char
			Boolean,  // TokenType::True or TokenType::False
			Null,     // TokenType::Null
		};

		LiteralNode() : m_Kind(LiteralKind::Number) {}

		// --- Getters ---

		const Core::String& GetValue() const { return m_ValueToken.GetText(); }
		const Token& GetValueToken() const { return m_ValueToken; }
		LiteralKind GetKind() const { return m_Kind; }
		/// The code-unit width of a character or string (1 char8, 2 char16, 4 char32).
		uint8 GetUnitWidth() const { return m_ValueToken.GetLiteralFacts().unitWidth; }

		/// Factory: peeks for a number, text, boolean, or null token. Creates a
		/// Ref<LiteralNode> and calls ParseBody. Returns an empty Ref if the
		/// current token is not a literal (no tokens consumed).
		static Ref<LiteralNode> TryParse(ParserState& state);

		/// Member: called after confirming the literal token. Consumes the token,
		/// stores its text and kind, and sets start/end locations from the token.
		void ParseBody(ParserState& state);

		Core::String ToString() const override;

	protected:
		NodeType Type() const override { return NodeType::LiteralExpression; }

	private:
		void JoinAdjacentStrings(ParserState& state);

		LiteralKind m_Kind;      // discriminant for the literal subtype
		Token m_ValueToken;    // raw literal token
	};
}
