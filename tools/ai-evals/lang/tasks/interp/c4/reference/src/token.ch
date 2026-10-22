include "bedrock.ch"

namespace Calc
{
	enum class TokenKind
	{
		EndOfInput, Number, String, Identifier,
		Let, Fn, If, Else, Return, While, True, False, Nil,
		LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket, Comma, Semicolon, Assign,
		Plus, Minus, Star, Slash, Percent, Bang,
		Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, And, Or
	}

	struct Token
	{
		public TokenKind kind;
		public Core::String lexeme;     // the source text, quotes and escapes included for a string
		public int64 number;            // a number's value
		public Core::String text;       // a string's value, escapes replaced
		public int64 line;
	}

	// The fixed text of a keyword, operator or punctuation token ("let", "+", "("); empty for the others.
	Core::StringView Spelling(TokenKind kind);
	// The keyword spelled `word`; false when it is an ordinary identifier.
	bool KeywordKind(Core::StringView word, TokenKind& kind);
	// How an error message names a kind of token: "'('", "identifier", "end of input".
	Core::String KindText(TokenKind kind);
	// How an error message names the token it found: "'x'" or "end of input".
	Core::String Describe(const Token& token);
}
