include "token.ch"
include "errors.ch"

namespace Calc
{
	// Turns the text of a script into tokens, one at a time. Lex errors are thrown as ScriptError.
	class Lexer
	{
		private Core::StringView source;
		private int64 position;
		private int64 line;

		[operator("create")] public void Init(Core::StringView source) {
			this.source = source;
			this.line = 1;
		}

		// The next token, skipping spaces, line breaks and // comments; at the end, EndOfInput.
		public Token Next();

		private int32 PeekAt(int64 offset) const;
		private void SkipSpaceAndComments();
		private void SkipComment();
		private Token MakeToken(TokenKind kind, int64 start) const;
		private Token LexNumber();
		private Token LexWord();
		private Token LexString();
		private char8 LexEscape();
		private Token LexOperator();
	}

	// Every token of `source`; the last one is EndOfInput.
	Core::Array<Token> Tokenize(Core::StringView source);
}
