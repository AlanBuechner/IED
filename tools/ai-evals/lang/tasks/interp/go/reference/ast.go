package main

// Expr is an expression of the syntax tree.
type Expr interface {
	Line() int
	exprNode()
}

// Stmt is a statement of the syntax tree.
type Stmt interface {
	Line() int
	stmtNode()
}

// position is the line a node starts on, which its runtime errors name.
type position struct {
	line int
}

func (p position) Line() int { return p.line }

func at(token Token) position { return position{line: token.Line} }

// LiteralExpr is a number, a string, true, false or nil.
type LiteralExpr struct {
	position
	Value Value
}

// VariableExpr reads a variable.
type VariableExpr struct {
	position
	Name string
}

// UnaryExpr is -operand or !operand.
type UnaryExpr struct {
	position
	Op      TokenKind
	Operand Expr
}

// BinaryExpr is an arithmetic, comparison or equality operator; its line is the operator's.
type BinaryExpr struct {
	position
	Op          TokenKind
	Left, Right Expr
}

// LogicalExpr is && or ||, which evaluate their right side only when it decides the result.
type LogicalExpr struct {
	position
	Op          TokenKind
	Left, Right Expr
}

// CallExpr calls a function; its line is the opening parenthesis's.
type CallExpr struct {
	position
	Callee    Expr
	Arguments []Expr
}

// ArrayExpr is an array literal: `[1, 2, 3]`.
type ArrayExpr struct {
	position
	Elements []Expr
}

// IndexExpr reads an element of an array: `array[index]`; its line is the opening bracket's.
type IndexExpr struct {
	position
	Array Expr
	Index Expr
}

// FunctionExpr is a function literal, `fn name(a, b) { ... }`; Name is "" for an anonymous one.
type FunctionExpr struct {
	position
	Name       string
	Parameters []string
	Body       []Stmt
}

func (*LiteralExpr) exprNode()  {}
func (*VariableExpr) exprNode() {}
func (*UnaryExpr) exprNode()    {}
func (*BinaryExpr) exprNode()   {}
func (*LogicalExpr) exprNode()  {}
func (*CallExpr) exprNode()     {}
func (*ArrayExpr) exprNode()    {}
func (*IndexExpr) exprNode()    {}
func (*FunctionExpr) exprNode() {}

// LetStmt declares a variable in the current scope: `let name = value;`.
type LetStmt struct {
	position
	Name  string
	Value Expr
}

// AssignStmt sets an existing variable: `name = value;`.
type AssignStmt struct {
	position
	Name  string
	Value Expr
}

// IndexAssignStmt sets an element of an array: `array[index] = value;`.
type IndexAssignStmt struct {
	position
	Target *IndexExpr
	Value  Expr
}

// ExpressionStmt evaluates an expression for its effects: `print(x);`.
type ExpressionStmt struct {
	position
	Expression Expr
}

// BlockStmt is `{ statements }`, a scope of its own.
type BlockStmt struct {
	position
	Statements []Stmt
}

// IfStmt is `if (condition) { ... }` with an optional else: nil, a *BlockStmt or an *IfStmt (else if).
type IfStmt struct {
	position
	Condition Expr
	Then      *BlockStmt
	Else      Stmt
}

// WhileStmt is `while (condition) { ... }`.
type WhileStmt struct {
	position
	Condition Expr
	Body      *BlockStmt
}

// FunctionStmt declares a named function in the current scope: `fn name(a) { ... }`.
type FunctionStmt struct {
	position
	Function *FunctionExpr
}

// ReturnStmt leaves the function being called; a nil Value returns nil.
type ReturnStmt struct {
	position
	Value Expr
}

func (*LetStmt) stmtNode()         {}
func (*AssignStmt) stmtNode()      {}
func (*IndexAssignStmt) stmtNode() {}
func (*ExpressionStmt) stmtNode()  {}
func (*BlockStmt) stmtNode()       {}
func (*IfStmt) stmtNode()          {}
func (*WhileStmt) stmtNode()       {}
func (*FunctionStmt) stmtNode()    {}
func (*ReturnStmt) stmtNode()      {}
