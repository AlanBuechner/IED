package main

// Binding strengths of the binary operators, loosest first.
const (
	precedenceNone = iota
	precedenceOr
	precedenceAnd
	precedenceEquality
	precedenceComparison
	precedenceSum
	precedenceProduct
)

// Precedence is the precedence table: how tightly a binary operator binds, precedenceNone for a token that is
// not a binary operator.
func Precedence(kind TokenKind) int {
	switch kind {
	case TokenOr:
		return precedenceOr
	case TokenAnd:
		return precedenceAnd
	case TokenEqual, TokenNotEqual:
		return precedenceEquality
	case TokenLess, TokenLessEqual, TokenGreater, TokenGreaterEqual:
		return precedenceComparison
	case TokenPlus, TokenMinus:
		return precedenceSum
	case TokenStar, TokenSlash, TokenPercent:
		return precedenceProduct
	}
	return precedenceNone
}

// ParseExpression parses a whole expression.
func (p *Parser) ParseExpression() (Expr, error) {
	return p.ParseBinary(precedenceOr)
}

// ParseBinary parses operands joined by binary operators that bind at least as tightly as minPrecedence
// (precedence climbing). The right side of an operator is parsed one level tighter, so `1 + 2 * 3` takes
// `2 * 3` as the right side of +, and operators of one level group to the left.
func (p *Parser) ParseBinary(minPrecedence int) (Expr, error) {
	left, err := p.ParseUnary()
	if err != nil {
		return nil, err
	}
	for {
		op := p.peek()
		precedence := Precedence(op.Kind)
		if precedence == precedenceNone || precedence < minPrecedence {
			return left, nil
		}
		p.advance()
		right, err := p.ParseBinary(precedence + 1)
		if err != nil {
			return nil, err
		}
		left = newBinary(op, left, right)
	}
}

func newBinary(op Token, left, right Expr) Expr {
	if op.Kind == TokenAnd || op.Kind == TokenOr {
		return &LogicalExpr{position: at(op), Op: op.Kind, Left: left, Right: right}
	}
	return &BinaryExpr{position: at(op), Op: op.Kind, Left: left, Right: right}
}

// ParseUnary parses the prefix operators - and !, which bind tighter than any binary operator.
func (p *Parser) ParseUnary() (Expr, error) {
	if !p.check(TokenMinus) && !p.check(TokenBang) {
		return p.ParseCall()
	}
	op := p.advance()
	operand, err := p.ParseUnary()
	if err != nil {
		return nil, err
	}
	return &UnaryExpr{position: at(op), Op: op.Kind, Operand: operand}, nil
}

// ParseCall parses a primary expression and the calls after it: `f(1)(2)`.
func (p *Parser) ParseCall() (Expr, error) {
	expr, err := p.ParsePrimary()
	if err != nil {
		return nil, err
	}
	for p.check(TokenLeftParen) {
		open := p.advance()
		arguments, err := p.parseExpressionList(TokenRightParen)
		if err != nil {
			return nil, err
		}
		expr = &CallExpr{position: at(open), Callee: expr, Arguments: arguments}
	}
	return expr, nil
}

// parseExpressionList parses `a, b, c` up to and including the closing token.
func (p *Parser) parseExpressionList(closing TokenKind) ([]Expr, error) {
	var items []Expr
	if p.match(closing) {
		return items, nil
	}
	for {
		item, err := p.ParseExpression()
		if err != nil {
			return nil, err
		}
		items = append(items, item)
		if !p.match(TokenComma) {
			break
		}
	}
	if _, err := p.expect(closing); err != nil {
		return nil, err
	}
	return items, nil
}

// ParsePrimary parses a literal, a variable, a parenthesized expression or a function literal.
func (p *Parser) ParsePrimary() (Expr, error) {
	token := p.peek()
	switch token.Kind {
	case TokenNumber:
		return p.literal(IntValue(token.Number)), nil
	case TokenString:
		return p.literal(StringValue(token.Text)), nil
	case TokenTrue:
		return p.literal(BoolValue(true)), nil
	case TokenFalse:
		return p.literal(BoolValue(false)), nil
	case TokenNil:
		return p.literal(NilValue{}), nil
	case TokenIdentifier:
		p.advance()
		return &VariableExpr{position: at(token), Name: token.Lexeme}, nil
	case TokenLeftParen:
		return p.parseGroup()
	case TokenFn:
		return p.parseFunction()
	}
	return nil, errorAt(token.Line, "expected expression but found %s", Describe(token))
}

// literal consumes the current token as the literal value.
func (p *Parser) literal(value Value) Expr {
	return &LiteralExpr{position: at(p.advance()), Value: value}
}

func (p *Parser) parseGroup() (Expr, error) {
	p.advance()
	expr, err := p.ParseExpression()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(TokenRightParen); err != nil {
		return nil, err
	}
	return expr, nil
}
