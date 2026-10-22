package main

const (
	monthLength  = 7
	monthsInYear = 12
)

// Options is what the command line asks for.
type Options struct {
	File    string
	Command string
	Account string // empty: every account
	Month   string // YYYY-MM; empty: every month
}

// ParseOptions reads the command line after the program name; false when it is not
// "<file> <command> [--account NAME] [--month YYYY-MM]".
func ParseOptions(args []string) (Options, bool) {
	if len(args) < 2 {
		return Options{}, false
	}
	options := Options{File: args[0], Command: args[1]}
	for i := 2; i < len(args); i += 2 {
		if i+1 >= len(args) {
			return Options{}, false
		}
		switch args[i] {
		case "--account":
			options.Account = args[i+1]
		case "--month":
			options.Month = args[i+1]
		default:
			return Options{}, false
		}
	}
	return options, true
}

func isDigit(c byte) bool { return c >= '0' && c <= '9' }

// IsMonth reports whether text is four digits, '-', and a month from 01 to 12.
func IsMonth(text string) bool {
	if len(text) != monthLength || text[4] != '-' {
		return false
	}
	for i := 0; i < monthLength; i++ {
		if i != 4 && !isDigit(text[i]) {
			return false
		}
	}
	month := int(text[5]-'0')*10 + int(text[6]-'0')
	return month >= 1 && month <= monthsInYear
}
