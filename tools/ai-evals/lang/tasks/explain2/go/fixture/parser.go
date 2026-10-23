package main

// Parser builds the syntax tree of a script from its tokens: statements here, expressions in expressions.go.
type Parser struct {
	tokens        []Token
	pos           int
	functionDepth int // how many function bodies the parser is inside; return is allowed only in one
}

// NewParser parses tokens, which end with a TokenEOF.
func NewParser(tokens []Token) *Parser {
	return &Parser{tokens: tokens}
}

// Parse reads a whole script into its statements.
func Parse(source string) ([]Stmt, error) {
	tokens, err := Tokenize(source)
	if err != nil {
		return nil, err
	}
	return NewParser(tokens).ParseProgram()
}

// ParseProgram parses statements up to the end of the input.
func (p *Parser) ParseProgram() ([]Stmt, error) {
	var statements []Stmt
	for !p.check(TokenEOF) {
		stmt, err := p.ParseStatement()
		if err != nil {
			return nil, err
		}
		statements = append(statements, stmt)
	}
	return statements, nil
}

// ParseStatement parses one statement.
func (p *Parser) ParseStatement() (Stmt, error) {
	switch p.peek().Kind {
	case TokenLet:
		return p.parseLet()
	case TokenIf:
		return p.parseIf()
	case TokenReturn:
		return p.parseReturn()
	case TokenLeftBrace:
		return p.ParseBlock()
	case TokenFn:
		if p.peekAt(1).Kind == TokenIdentifier {
			return p.parseFunctionStatement()
		}
	}
	return p.parseExpressionStatement()
}

// ParseBlock parses `{ statements }`.
func (p *Parser) ParseBlock() (*BlockStmt, error) {
	open, err := p.expect(TokenLeftBrace)
	if err != nil {
		return nil, err
	}
	block := &BlockStmt{position: at(open)}
	for !p.check(TokenRightBrace) && !p.check(TokenEOF) {
		stmt, err := p.ParseStatement()
		if err != nil {
			return nil, err
		}
		block.Statements = append(block.Statements, stmt)
	}
	if _, err := p.expect(TokenRightBrace); err != nil {
		return nil, err
	}
	return block, nil
}

func (p *Parser) parseLet() (Stmt, error) {
	keyword := p.advance()
	name, err := p.expect(TokenIdentifier)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(TokenAssign); err != nil {
		return nil, err
	}
	value, err := p.parseTerminatedExpression()
	if err != nil {
		return nil, err
	}
	return &LetStmt{position: at(keyword), Name: name.Lexeme, Value: value}, nil
}

func (p *Parser) parseIf() (Stmt, error) {
	keyword := p.advance()
	condition, err := p.parseCondition()
	if err != nil {
		return nil, err
	}
	then, err := p.ParseBlock()
	if err != nil {
		return nil, err
	}
	stmt := &IfStmt{position: at(keyword), Condition: condition, Then: then}
	if !p.match(TokenElse) {
		return stmt, nil
	}
	if p.check(TokenIf) {
		stmt.Else, err = p.parseIf()
	} else {
		stmt.Else, err = p.ParseBlock()
	}
	if err != nil {
		return nil, err
	}
	return stmt, nil
}

// parseCondition parses the parenthesized condition of an if.
func (p *Parser) parseCondition() (Expr, error) {
	if _, err := p.expect(TokenLeftParen); err != nil {
		return nil, err
	}
	condition, err := p.ParseExpression()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(TokenRightParen); err != nil {
		return nil, err
	}
	return condition, nil
}

func (p *Parser) parseFunctionStatement() (Stmt, error) {
	function, err := p.parseFunction()
	if err != nil {
		return nil, err
	}
	return &FunctionStmt{position: function.position, Function: function}, nil
}

// parseFunction parses `fn name(parameters) { body }`, the name optional.
func (p *Parser) parseFunction() (*FunctionExpr, error) {
	keyword := p.advance()
	function := &FunctionExpr{position: at(keyword)}
	if p.check(TokenIdentifier) {
		function.Name = p.advance().Lexeme
	}
	parameters, err := p.parseParameters()
	if err != nil {
		return nil, err
	}
	function.Parameters = parameters
	p.functionDepth++
	body, err := p.ParseBlock()
	p.functionDepth--
	if err != nil {
		return nil, err
	}
	function.Body = body.Statements
	return function, nil
}

func (p *Parser) parseParameters() ([]string, error) {
	if _, err := p.expect(TokenLeftParen); err != nil {
		return nil, err
	}
	var names []string
	if p.match(TokenRightParen) {
		return names, nil
	}
	for {
		name, err := p.expect(TokenIdentifier)
		if err != nil {
			return nil, err
		}
		names = append(names, name.Lexeme)
		if !p.match(TokenComma) {
			break
		}
	}
	if _, err := p.expect(TokenRightParen); err != nil {
		return nil, err
	}
	return names, nil
}

func (p *Parser) parseReturn() (Stmt, error) {
	keyword := p.advance()
	if p.functionDepth == 0 {
		return nil, errorAt(keyword.Line, "return outside of a function")
	}
	stmt := &ReturnStmt{position: at(keyword)}
	if p.match(TokenSemicolon) {
		return stmt, nil
	}
	value, err := p.parseTerminatedExpression()
	if err != nil {
		return nil, err
	}
	stmt.Value = value
	return stmt, nil
}

// parseExpressionStatement parses `expression;`, or an assignment when an = follows the expression.
func (p *Parser) parseExpressionStatement() (Stmt, error) {
	expr, err := p.ParseExpression()
	if err != nil {
		return nil, err
	}
	if p.check(TokenAssign) {
		return p.parseAssignment(expr)
	}
	if _, err := p.expect(TokenSemicolon); err != nil {
		return nil, err
	}
	return &ExpressionStmt{position: position{line: expr.Line()}, Expression: expr}, nil
}

// parseAssignment parses the rest of `target = value;` once target is read.
func (p *Parser) parseAssignment(target Expr) (Stmt, error) {
	equals := p.advance()
	variable, ok := target.(*VariableExpr)
	if !ok {
		return nil, errorAt(equals.Line, "invalid assignment target")
	}
	value, err := p.parseTerminatedExpression()
	if err != nil {
		return nil, err
	}
	return &AssignStmt{position: variable.position, Name: variable.Name, Value: value}, nil
}

// parseTerminatedExpression parses an expression and the ; after it.
func (p *Parser) parseTerminatedExpression() (Expr, error) {
	expr, err := p.ParseExpression()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(TokenSemicolon); err != nil {
		return nil, err
	}
	return expr, nil
}

// --- Reading tokens ----------------------------------------------------------------------------------------------

func (p *Parser) peek() Token {
	return p.peekAt(0)
}

// peekAt is the token offset places ahead; past the end it is the final TokenEOF.
func (p *Parser) peekAt(offset int) Token {
	if p.pos+offset >= len(p.tokens) {
		return p.tokens[len(p.tokens)-1]
	}
	return p.tokens[p.pos+offset]
}

func (p *Parser) advance() Token {
	token := p.peek()
	if p.pos < len(p.tokens)-1 {
		p.pos++
	}
	return token
}

func (p *Parser) check(kind TokenKind) bool {
	return p.peek().Kind == kind
}

// match consumes the next token when it is of kind.
func (p *Parser) match(kind TokenKind) bool {
	if !p.check(kind) {
		return false
	}
	p.advance()
	return true
}

// expect consumes the next token, which must be of kind.
func (p *Parser) expect(kind TokenKind) (Token, error) {
	token := p.peek()
	if token.Kind != kind {
		return Token{}, errorAt(token.Line, "expected %s but found %s", KindText(kind), Describe(token))
	}
	return p.advance(), nil
}
