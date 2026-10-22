package main

// Options is what the command line asks for.
type Options struct {
	File    string
	Command string
	Account string // empty: every account
}

// ParseOptions reads the command line after the program name; false when it is not "<file> <command> [--account NAME]".
func ParseOptions(args []string) (Options, bool) {
	if len(args) < 2 {
		return Options{}, false
	}
	options := Options{File: args[0], Command: args[1]}
	for i := 2; i < len(args); i += 2 {
		if i+1 >= len(args) || args[i] != "--account" {
			return Options{}, false
		}
		options.Account = args[i+1]
	}
	return options, true
}
