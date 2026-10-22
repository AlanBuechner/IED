package main

import "strings"

// IsSpace reports whether c is a space or a tab: the whitespace of the block structure.
func IsSpace(c byte) bool { return c == ' ' || c == '\t' }

// TrimStart is the text without its leading spaces and tabs.
func TrimStart(text string) string {
	start := 0
	for start < len(text) && IsSpace(text[start]) {
		start++
	}
	return text[start:]
}

// Trim is the text without its leading and trailing spaces and tabs.
func Trim(text string) string {
	text = TrimStart(text)
	end := len(text)
	for end > 0 && IsSpace(text[end-1]) {
		end--
	}
	return text[:end]
}

// IsBlank reports whether the line holds nothing but spaces and tabs.
func IsBlank(line string) bool { return TrimStart(line) == "" }

// Indentation is the number of leading spaces and tabs (a tab counts as one).
func Indentation(line string) int { return len(line) - len(TrimStart(line)) }

// SplitLines gives the lines of a document: split at '\n', a '\r' ending a line dropped; a final '\n' starts no
// further line.
func SplitLines(text string) []string {
	var lines []string
	start := 0
	for start < len(text) {
		end := strings.IndexByte(text[start:], '\n')
		if end < 0 {
			end = len(text)
		} else {
			end += start
		}
		lines = append(lines, strings.TrimSuffix(text[start:end], "\r"))
		start = end + 1
	}
	return lines
}

// AppendEscaped appends the text with &, <, > and " written as entities.
func AppendEscaped(out *strings.Builder, text string) {
	for i := 0; i < len(text); i++ {
		switch c := text[i]; c {
		case '&':
			out.WriteString("&amp;")
		case '<':
			out.WriteString("&lt;")
		case '>':
			out.WriteString("&gt;")
		case '"':
			out.WriteString("&quot;")
		default:
			out.WriteByte(c)
		}
	}
}
