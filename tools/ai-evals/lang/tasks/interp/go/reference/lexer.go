package main

import (
	"math"
	"strings"
)

// Lexer turns the text of a script into tokens, one at a time.
type Lexer struct {
	source string
	pos    int
	line   int
}

// NewLexer starts reading source at its first line.
func NewLexer(source string) *Lexer {
	return &Lexer{source: source, line: 1}
}

// Tokenize reads every token of source; the last one is a TokenEOF.
func Tokenize(source string) ([]Token, error) {
	lexer := NewLexer(source)
	var tokens []Token
	for {
		token, err := lexer.Next()
		if err != nil {
			return nil, err
		}
		tokens = append(tokens, token)
		if token.Kind == TokenEOF {
			return tokens, nil
		}
	}
}

// Next reads the next token, skipping spaces, line breaks and // comments; at the end it gives TokenEOF.
func (l *Lexer) Next() (Token, error) {
	l.skipSpaceAndComments()
	if l.pos >= len(l.source) {
		return Token{Kind: TokenEOF, Line: l.line}, nil
	}
	c := l.source[l.pos]
	switch {
	case isDigit(c):
		return l.lexNumber()
	case isIdentifierStart(c):
		return l.lexWord(), nil
	case c == '"':
		return l.lexString()
	}
	return l.lexOperator()
}

func (l *Lexer) peekAt(offset int) byte {
	if l.pos+offset >= len(l.source) {
		return 0
	}
	return l.source[l.pos+offset]
}

func (l *Lexer) skipSpaceAndComments() {
	for l.pos < len(l.source) {
		switch c := l.source[l.pos]; {
		case c == '\n':
			l.line++
			l.pos++
		case c == ' ' || c == '\t' || c == '\r':
			l.pos++
		case c == '/' && l.peekAt(1) == '/':
			l.skipComment()
		default:
			return
		}
	}
}

func (l *Lexer) skipComment() {
	for l.pos < len(l.source) && l.source[l.pos] != '\n' {
		l.pos++
	}
}

// token makes a token of the source text from start to the current position.
func (l *Lexer) token(kind TokenKind, start int) Token {
	return Token{Kind: kind, Lexeme: l.source[start:l.pos], Line: l.line}
}

func (l *Lexer) lexNumber() (Token, error) {
	start := l.pos
	var value int64
	for l.pos < len(l.source) && isDigit(l.source[l.pos]) {
		digit := int64(l.source[l.pos] - '0')
		if value > (math.MaxInt64-digit)/10 {
			return Token{}, errorAt(l.line, "number too large")
		}
		value = value*10 + digit
		l.pos++
	}
	token := l.token(TokenNumber, start)
	token.Number = value
	return token, nil
}

func (l *Lexer) lexWord() Token {
	start := l.pos
	for l.pos < len(l.source) && isIdentifierPart(l.source[l.pos]) {
		l.pos++
	}
	kind, isKeyword := KeywordKind(l.source[start:l.pos])
	if !isKeyword {
		kind = TokenIdentifier
	}
	return l.token(kind, start)
}

// lexString reads a string literal; it ends on the line it starts.
func (l *Lexer) lexString() (Token, error) {
	start := l.pos
	l.pos++
	var text strings.Builder
	for {
		if l.pos >= len(l.source) || l.source[l.pos] == '\n' {
			return Token{}, errorAt(l.line, "unterminated string")
		}
		c := l.source[l.pos]
		l.pos++
		if c == '"' {
			break
		}
		if c == '\\' {
			escaped, err := l.lexEscape()
			if err != nil {
				return Token{}, err
			}
			c = escaped
		}
		text.WriteByte(c)
	}
	token := l.token(TokenString, start)
	token.Text = text.String()
	return token, nil
}

// lexEscape reads the character after a backslash: \n, \" or \\.
func (l *Lexer) lexEscape() (byte, error) {
	if l.pos >= len(l.source) || l.source[l.pos] == '\n' {
		return 0, errorAt(l.line, "unterminated string")
	}
	c := l.source[l.pos]
	l.pos++
	switch c {
	case 'n':
		return '\n', nil
	case '"', '\\':
		return c, nil
	}
	return 0, errorAt(l.line, "unknown escape '\\%s'", l.source[l.pos-1:l.pos])
}

func (l *Lexer) lexOperator() (Token, error) {
	start := l.pos
	c := l.source[l.pos]
	if kind, ok := twoCharOperator(c, l.peekAt(1)); ok {
		l.pos += 2
		return l.token(kind, start), nil
	}
	if kind, ok := oneCharOperator(c); ok {
		l.pos++
		return l.token(kind, start), nil
	}
	return Token{}, errorAt(l.line, "unexpected character '%s'", l.source[start:start+1])
}

func twoCharOperator(first, second byte) (TokenKind, bool) {
	switch string([]byte{first, second}) {
	case "==":
		return TokenEqual, true
	case "!=":
		return TokenNotEqual, true
	case "<=":
		return TokenLessEqual, true
	case ">=":
		return TokenGreaterEqual, true
	case "&&":
		return TokenAnd, true
	case "||":
		return TokenOr, true
	}
	return TokenEOF, false
}

func oneCharOperator(c byte) (TokenKind, bool) {
	switch c {
	case '(':
		return TokenLeftParen, true
	case ')':
		return TokenRightParen, true
	case '{':
		return TokenLeftBrace, true
	case '}':
		return TokenRightBrace, true
	case '[':
		return TokenLeftBracket, true
	case ']':
		return TokenRightBracket, true
	case ',':
		return TokenComma, true
	case ';':
		return TokenSemicolon, true
	case '=':
		return TokenAssign, true
	case '+':
		return TokenPlus, true
	case '-':
		return TokenMinus, true
	case '*':
		return TokenStar, true
	case '/':
		return TokenSlash, true
	case '%':
		return TokenPercent, true
	case '!':
		return TokenBang, true
	case '<':
		return TokenLess, true
	case '>':
		return TokenGreater, true
	}
	return TokenEOF, false
}

func isDigit(c byte) bool {
	return c >= '0' && c <= '9'
}

func isIdentifierStart(c byte) bool {
	return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_'
}

func isIdentifierPart(c byte) bool {
	return isIdentifierStart(c) || isDigit(c)
}
