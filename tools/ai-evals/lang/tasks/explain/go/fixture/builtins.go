package main

import (
	"io"
	"strings"
)

// BuiltinCall is what a builtin function is given: its arguments (already counted against its arity), the
// line of the call for errors, and where print writes.
type BuiltinCall struct {
	Arguments []Value
	Line      int
	Out       io.Writer
}

// DefineBuiltins defines the builtin functions in the global scope.
func DefineBuiltins(globals *Environment) {
	builtins := []*BuiltinValue{
		{Name: "print", Arity: -1, Call: builtinPrint},
		{Name: "str", Arity: 1, Call: builtinStr},
	}
	for _, builtin := range builtins {
		globals.Define(builtin.Name, builtin)
	}
}

// builtinPrint writes its arguments separated by spaces, then a line break.
func builtinPrint(call BuiltinCall) (Value, error) {
	parts := make([]string, len(call.Arguments))
	for i, argument := range call.Arguments {
		parts[i] = FormatValue(argument)
	}
	if _, err := io.WriteString(call.Out, strings.Join(parts, " ")+"\n"); err != nil {
		return nil, err
	}
	return NilValue{}, nil
}

// builtinStr is its argument as print writes it.
func builtinStr(call BuiltinCall) (Value, error) {
	return StringValue(FormatValue(call.Arguments[0])), nil
}
