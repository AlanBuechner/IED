package main

// TokenKind is what a token is: a literal, a name, a keyword or a piece of punctuation.
type TokenKind int

const (
	TokenEOF TokenKind = iota
	TokenNumber
	TokenString
	TokenIdentifier

	TokenLet
	TokenFn
	TokenIf
	TokenElse
	TokenReturn
	TokenTrue
	TokenFalse
	TokenNil

	TokenLeftParen
	TokenRightParen
	TokenLeftBrace
	TokenRightBrace
	TokenComma
	TokenSemicolon
	TokenAssign
	TokenPlus
	TokenMinus
	TokenStar
	TokenSlash
	TokenPercent
	TokenBang
	TokenEqual
	TokenNotEqual
	TokenLess
	TokenLessEqual
	TokenGreater
	TokenGreaterEqual
	TokenAnd
	TokenOr
)

// Token is one token of a script.
type Token struct {
	Kind   TokenKind
	Lexeme string // the source text, quotes and escapes included for a string
	Number int64  // a number's value
	Text   string // a string's value, escapes replaced
	Line   int
}

// Spelling is the fixed text of a keyword, operator or punctuation token ("let", "+", "("); "" for the others.
func Spelling(kind TokenKind) string {
	switch kind {
	case TokenLet:
		return "let"
	case TokenFn:
		return "fn"
	case TokenIf:
		return "if"
	case TokenElse:
		return "else"
	case TokenReturn:
		return "return"
	case TokenTrue:
		return "true"
	case TokenFalse:
		return "false"
	case TokenNil:
		return "nil"
	}
	return punctuationSpelling(kind)
}

func punctuationSpelling(kind TokenKind) string {
	switch kind {
	case TokenLeftParen:
		return "("
	case TokenRightParen:
		return ")"
	case TokenLeftBrace:
		return "{"
	case TokenRightBrace:
		return "}"
	case TokenComma:
		return ","
	case TokenSemicolon:
		return ";"
	case TokenAssign:
		return "="
	}
	return operatorSpelling(kind)
}

func operatorSpelling(kind TokenKind) string {
	switch kind {
	case TokenPlus:
		return "+"
	case TokenMinus:
		return "-"
	case TokenStar:
		return "*"
	case TokenSlash:
		return "/"
	case TokenPercent:
		return "%"
	case TokenBang:
		return "!"
	case TokenEqual:
		return "=="
	case TokenNotEqual:
		return "!="
	case TokenLess:
		return "<"
	case TokenLessEqual:
		return "<="
	case TokenGreater:
		return ">"
	case TokenGreaterEqual:
		return ">="
	case TokenAnd:
		return "&&"
	case TokenOr:
		return "||"
	}
	return ""
}

// KeywordKind is the keyword spelled word; false when word is an ordinary identifier.
func KeywordKind(word string) (TokenKind, bool) {
	switch word {
	case "let":
		return TokenLet, true
	case "fn":
		return TokenFn, true
	case "if":
		return TokenIf, true
	case "else":
		return TokenElse, true
	case "return":
		return TokenReturn, true
	case "true":
		return TokenTrue, true
	case "false":
		return TokenFalse, true
	case "nil":
		return TokenNil, true
	}
	return TokenIdentifier, false
}

// KindText is how an error message names a kind of token: "'('", "identifier", "end of input".
func KindText(kind TokenKind) string {
	switch kind {
	case TokenEOF:
		return "end of input"
	case TokenNumber:
		return "number"
	case TokenString:
		return "string"
	case TokenIdentifier:
		return "identifier"
	}
	return "'" + Spelling(kind) + "'"
}

// Describe is how an error message names the token it found: "'x'" or "end of input".
func Describe(token Token) string {
	if token.Kind == TokenEOF {
		return "end of input"
	}
	return "'" + token.Lexeme + "'"
}
