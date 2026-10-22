package main

import "io"

// Interpreter runs a parsed script by walking its syntax tree; what the script prints goes to out.
type Interpreter struct {
	Globals *Environment
	out     io.Writer
}

// Flow is how a statement ended: normally, or by a return and the value returned.
type Flow struct {
	Returned bool
	Value    Value
}

// NewInterpreter makes an interpreter whose global scope holds the builtins.
func NewInterpreter(out io.Writer) *Interpreter {
	interpreter := &Interpreter{Globals: NewEnvironment(nil), out: out}
	DefineBuiltins(interpreter.Globals)
	return interpreter
}

// RunScript parses and runs a script, writing what it prints to out.
func RunScript(source string, out io.Writer) error {
	program, err := Parse(source)
	if err != nil {
		return err
	}
	return NewInterpreter(out).Run(program)
}

// Run executes a script's statements in the global scope.
func (in *Interpreter) Run(program []Stmt) error {
	_, err := in.ExecuteBlock(program, in.Globals)
	return err
}

// ExecuteBlock executes statements in order in env, stopping at a return.
func (in *Interpreter) ExecuteBlock(statements []Stmt, env *Environment) (Flow, error) {
	for _, stmt := range statements {
		flow, err := in.Execute(stmt, env)
		if err != nil || flow.Returned {
			return flow, err
		}
	}
	return Flow{}, nil
}

// Execute executes one statement in env.
func (in *Interpreter) Execute(stmt Stmt, env *Environment) (Flow, error) {
	switch s := stmt.(type) {
	case *LetStmt:
		return Flow{}, in.executeLet(s, env)
	case *AssignStmt:
		return Flow{}, in.executeAssign(s, env)
	case *ExpressionStmt:
		_, err := in.Evaluate(s.Expression, env)
		return Flow{}, err
	case *BlockStmt:
		return in.ExecuteBlock(s.Statements, NewEnvironment(env))
	case *IfStmt:
		return in.executeIf(s, env)
	case *FunctionStmt:
		env.Define(s.Function.Name, &FunctionValue{Declaration: s.Function, Closure: env})
		return Flow{}, nil
	case *ReturnStmt:
		return in.executeReturn(s, env)
	}
	return Flow{}, errorAt(stmt.Line(), "cannot execute %T", stmt)
}

func (in *Interpreter) executeLet(s *LetStmt, env *Environment) error {
	value, err := in.Evaluate(s.Value, env)
	if err != nil {
		return err
	}
	env.Define(s.Name, value)
	return nil
}

func (in *Interpreter) executeAssign(s *AssignStmt, env *Environment) error {
	value, err := in.Evaluate(s.Value, env)
	if err != nil {
		return err
	}
	if !env.Assign(s.Name, value) {
		return errorAt(s.Line(), "undefined variable '%s'", s.Name)
	}
	return nil
}

func (in *Interpreter) executeIf(s *IfStmt, env *Environment) (Flow, error) {
	condition, err := in.Evaluate(s.Condition, env)
	if err != nil {
		return Flow{}, err
	}
	if IsTruthy(condition) {
		return in.Execute(s.Then, env)
	}
	if s.Else != nil {
		return in.Execute(s.Else, env)
	}
	return Flow{}, nil
}

func (in *Interpreter) executeReturn(s *ReturnStmt, env *Environment) (Flow, error) {
	if s.Value == nil {
		return Flow{Returned: true, Value: NilValue{}}, nil
	}
	value, err := in.Evaluate(s.Value, env)
	if err != nil {
		return Flow{}, err
	}
	return Flow{Returned: true, Value: value}, nil
}

// Evaluate computes the value of an expression in env.
func (in *Interpreter) Evaluate(expr Expr, env *Environment) (Value, error) {
	switch e := expr.(type) {
	case *LiteralExpr:
		return e.Value, nil
	case *VariableExpr:
		return in.lookupVariable(e, env)
	case *UnaryExpr:
		operand, err := in.Evaluate(e.Operand, env)
		if err != nil {
			return nil, err
		}
		return ApplyUnary(e.Op, operand, e.Line())
	case *BinaryExpr:
		return in.evaluateBinary(e, env)
	case *LogicalExpr:
		return in.evaluateLogical(e, env)
	case *CallExpr:
		return in.evaluateCall(e, env)
	case *FunctionExpr:
		return &FunctionValue{Declaration: e, Closure: env}, nil
	}
	return nil, errorAt(expr.Line(), "cannot evaluate %T", expr)
}

func (in *Interpreter) lookupVariable(e *VariableExpr, env *Environment) (Value, error) {
	value, ok := env.Lookup(e.Name)
	if !ok {
		return nil, errorAt(e.Line(), "undefined variable '%s'", e.Name)
	}
	return value, nil
}

func (in *Interpreter) evaluateBinary(e *BinaryExpr, env *Environment) (Value, error) {
	left, err := in.Evaluate(e.Left, env)
	if err != nil {
		return nil, err
	}
	right, err := in.Evaluate(e.Right, env)
	if err != nil {
		return nil, err
	}
	return ApplyBinary(e.Op, left, right, e.Line())
}

// evaluateLogical evaluates && and ||: the right side only when the left one does not decide the result.
func (in *Interpreter) evaluateLogical(e *LogicalExpr, env *Environment) (Value, error) {
	left, err := in.Evaluate(e.Left, env)
	if err != nil {
		return nil, err
	}
	if e.Op == TokenOr && IsTruthy(left) {
		return BoolValue(true), nil
	}
	if e.Op == TokenAnd && !IsTruthy(left) {
		return BoolValue(false), nil
	}
	right, err := in.Evaluate(e.Right, env)
	if err != nil {
		return nil, err
	}
	return BoolValue(IsTruthy(right)), nil
}

func (in *Interpreter) evaluateCall(e *CallExpr, env *Environment) (Value, error) {
	callee, err := in.Evaluate(e.Callee, env)
	if err != nil {
		return nil, err
	}
	arguments, err := in.evaluateAll(e.Arguments, env)
	if err != nil {
		return nil, err
	}
	switch function := callee.(type) {
	case *FunctionValue:
		return in.CallFunction(function, arguments, e.Line())
	case *BuiltinValue:
		return in.callBuiltin(function, arguments, e.Line())
	}
	return nil, errorAt(e.Line(), "cannot call %s", callee.TypeName())
}

// evaluateAll evaluates expressions left to right.
func (in *Interpreter) evaluateAll(exprs []Expr, env *Environment) ([]Value, error) {
	values := make([]Value, 0, len(exprs))
	for _, expr := range exprs {
		value, err := in.Evaluate(expr, env)
		if err != nil {
			return nil, err
		}
		values = append(values, value)
	}
	return values, nil
}

// CallFunction calls a function of the script. Its body runs in a new environment whose parent is the
// function's closure, so it sees the variables of the scope it was defined in; the parameters are defined in
// that environment, bound to the arguments in order.
func (in *Interpreter) CallFunction(function *FunctionValue, arguments []Value, line int) (Value, error) {
	parameters := function.Declaration.Parameters
	if err := checkArgumentCount(len(parameters), len(arguments), line); err != nil {
		return nil, err
	}
	env := NewEnvironment(function.Closure)
	for i, name := range parameters {
		env.Define(name, arguments[i])
	}
	flow, err := in.ExecuteBlock(function.Declaration.Body, env)
	if err != nil {
		return nil, err
	}
	if !flow.Returned {
		return NilValue{}, nil
	}
	return flow.Value, nil
}

func (in *Interpreter) callBuiltin(builtin *BuiltinValue, arguments []Value, line int) (Value, error) {
	if builtin.Arity >= 0 {
		if err := checkArgumentCount(builtin.Arity, len(arguments), line); err != nil {
			return nil, err
		}
	}
	return builtin.Call(BuiltinCall{Arguments: arguments, Line: line, Out: in.out})
}

func checkArgumentCount(expected, got, line int) error {
	if expected == got {
		return nil
	}
	noun := "arguments"
	if expected == 1 {
		noun = "argument"
	}
	return errorAt(line, "expected %d %s but got %d", expected, noun, got)
}
