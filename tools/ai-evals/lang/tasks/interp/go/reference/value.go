package main

import (
	"strconv"
	"strings"
)

// Value is a value of a script: IntValue, StringValue, BoolValue, NilValue, *ArrayValue, *FunctionValue or
// *BuiltinValue.
type Value interface {
	TypeName() string
}

// IntValue is a 64-bit integer.
type IntValue int64

// StringValue is a string of bytes.
type StringValue string

// BoolValue is true or false.
type BoolValue bool

// NilValue is nil, the value of a function that returns nothing.
type NilValue struct{}

// ArrayValue is an array. Arrays are shared, not copied: every variable holding one sees its changes.
type ArrayValue struct {
	Elements []Value
}

// FunctionValue is a function of the script together with its closure: the environment it was defined in,
// whose variables its body can read and assign.
type FunctionValue struct {
	Declaration *FunctionExpr
	Closure     *Environment
}

// BuiltinValue is a function the interpreter provides, such as print.
type BuiltinValue struct {
	Name  string
	Arity int // -1: any number of arguments
	Call  func(call BuiltinCall) (Value, error)
}

func (IntValue) TypeName() string       { return "int" }
func (StringValue) TypeName() string    { return "string" }
func (BoolValue) TypeName() string      { return "bool" }
func (NilValue) TypeName() string       { return "nil" }
func (*ArrayValue) TypeName() string    { return "array" }
func (*FunctionValue) TypeName() string { return "function" }
func (*BuiltinValue) TypeName() string  { return "function" }

// IsTruthy is whether a condition holds for value: everything but false and nil does.
func IsTruthy(value Value) bool {
	switch v := value.(type) {
	case BoolValue:
		return bool(v)
	case NilValue:
		return false
	}
	return true
}

// ValuesEqual is ==: values of different types are never equal; arrays and functions are equal only to themselves.
func ValuesEqual(a, b Value) bool {
	return a == b
}

// FormatValue is how print writes a value: strings without quotes, arrays as [1, 2, 3], functions as <fn name>.
func FormatValue(value Value) string {
	switch v := value.(type) {
	case IntValue:
		return strconv.FormatInt(int64(v), 10)
	case StringValue:
		return string(v)
	case BoolValue:
		return strconv.FormatBool(bool(v))
	case NilValue:
		return "nil"
	case *ArrayValue:
		return formatArray(v)
	case *FunctionValue:
		if v.Declaration.Name == "" {
			return "<fn>"
		}
		return "<fn " + v.Declaration.Name + ">"
	case *BuiltinValue:
		return "<builtin " + v.Name + ">"
	}
	return "<" + value.TypeName() + ">"
}

func formatArray(array *ArrayValue) string {
	parts := make([]string, len(array.Elements))
	for i, element := range array.Elements {
		parts[i] = FormatValue(element)
	}
	return "[" + strings.Join(parts, ", ") + "]"
}
