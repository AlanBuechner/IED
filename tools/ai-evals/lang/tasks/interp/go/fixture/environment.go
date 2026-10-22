package main

// Environment is one scope of variables. Parent is the scope it is nested in: the enclosing block, or for a
// function call the function's closure; nil for the global scope.
type Environment struct {
	values map[string]Value
	Parent *Environment
}

// NewEnvironment makes an empty scope inside parent.
func NewEnvironment(parent *Environment) *Environment {
	return &Environment{values: make(map[string]Value), Parent: parent}
}

// Define creates name in this scope, or replaces it when this scope already has it.
func (e *Environment) Define(name string, value Value) {
	e.values[name] = value
}

// Assign sets name in the nearest scope that has it; false when none does.
func (e *Environment) Assign(name string, value Value) bool {
	for scope := e; scope != nil; scope = scope.Parent {
		if _, ok := scope.values[name]; ok {
			scope.values[name] = value
			return true
		}
	}
	return false
}

// Lookup reads name from the nearest scope that has it; false when none does.
func (e *Environment) Lookup(name string) (Value, bool) {
	for scope := e; scope != nil; scope = scope.Parent {
		if value, ok := scope.values[name]; ok {
			return value, true
		}
	}
	return nil, false
}
