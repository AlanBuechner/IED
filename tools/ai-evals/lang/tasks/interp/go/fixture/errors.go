package main

import "fmt"

// ScriptError is a problem in a script - a lex, parse or runtime error - and the line it is on.
type ScriptError struct {
	Line    int
	Message string
}

func (e *ScriptError) Error() string {
	return fmt.Sprintf("line %d: %s", e.Line, e.Message)
}

// errorAt makes the error for line, its message formatted as fmt.Sprintf does.
func errorAt(line int, format string, args ...any) *ScriptError {
	return &ScriptError{Line: line, Message: fmt.Sprintf(format, args...)}
}
