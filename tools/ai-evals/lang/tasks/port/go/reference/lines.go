package main

import "strings"

// LineKind is what one line of Markdown is, told by its first characters: the vocabulary of the block parser.
type LineKind int

const (
	LineBlank LineKind = iota
	LineFence
	LineHeading
	LineRule
	LineQuote
	LineListItem
	LineText
)

const (
	minFenceTicks   = 3
	maxHeadingLevel = 6
	minRuleMarks    = 3
	maxNumberDigits = 9
)

// Heading is a heading line: its level (1 to 6) and its text without the marker and a closing run of '#'.
type Heading struct {
	Level int
	Text  string
}

// Fence is an opening fence: the length of its backtick run (3 or more) and the first word of its info string.
type Fence struct {
	Ticks    int
	Language string
}

// ListMarker is a list item line: its indentation, kind, number (ordered) and the text after the marker, trimmed.
type ListMarker struct {
	Indent  int
	Ordered bool
	Number  int
	Text    string
}

func isDigit(c byte) bool { return c >= '0' && c <= '9' }

func countLeading(text string, c byte) int {
	count := 0
	for count < len(text) && text[count] == c {
		count++
	}
	return count
}

// stripClosingHashes removes a closing run of '#' when it is all there is or follows whitespace: "Title ##" is
// "Title", "C#" stays.
func stripClosingHashes(text string) string {
	end := len(text)
	for end > 0 && text[end-1] == '#' {
		end--
	}
	if end == len(text) || (end > 0 && !IsSpace(text[end-1])) {
		return text
	}
	return Trim(text[:end])
}

// Classify gives the kind of a line, tested in this order: blank, fence, heading, rule, quote, list item;
// anything else is text.
func Classify(line string) LineKind {
	if IsBlank(line) {
		return LineBlank
	}
	if _, ok := MatchFence(line); ok {
		return LineFence
	}
	if _, ok := MatchHeading(line); ok {
		return LineHeading
	}
	if IsRule(line) {
		return LineRule
	}
	if _, ok := MatchQuote(line); ok {
		return LineQuote
	}
	if _, ok := MatchListItem(line); ok {
		return LineListItem
	}
	return LineText
}

// MatchFence reads "```lang": three or more backticks; an info string with a backtick in it makes the line no fence.
func MatchFence(line string) (Fence, bool) {
	rest := TrimStart(line)
	ticks := countLeading(rest, '`')
	if ticks < minFenceTicks {
		return Fence{}, false
	}
	info := Trim(rest[ticks:])
	if strings.IndexByte(info, '`') >= 0 {
		return Fence{}, false
	}
	word := 0
	for word < len(info) && !IsSpace(info[word]) {
		word++
	}
	return Fence{Ticks: ticks, Language: info[:word]}, true
}

// ClosesFence reports whether the line is a run of at least `ticks` backticks with nothing but whitespace after it.
func ClosesFence(line string, ticks int) bool {
	rest := TrimStart(line)
	run := countLeading(rest, '`')
	return run >= ticks && IsBlank(rest[run:])
}

// MatchHeading reads "## Title": one to six '#' followed by whitespace or the end of the line.
func MatchHeading(line string) (Heading, bool) {
	rest := TrimStart(line)
	level := countLeading(rest, '#')
	if level == 0 || level > maxHeadingLevel {
		return Heading{}, false
	}
	if level < len(rest) && !IsSpace(rest[level]) {
		return Heading{}, false
	}
	return Heading{Level: level, Text: stripClosingHashes(Trim(rest[level:]))}, true
}

// IsRule reports whether the line is three or more '-', or three or more '*', and nothing else but whitespace.
func IsRule(line string) bool {
	var mark byte
	count := 0
	for i := 0; i < len(line); i++ {
		c := line[i]
		if IsSpace(c) {
			continue
		}
		if (c != '-' && c != '*') || (mark != 0 && c != mark) {
			return false
		}
		mark = c
		count++
	}
	return count >= minRuleMarks
}

// MatchQuote reads "> text": what follows the '>' and the one space or tab after it.
func MatchQuote(line string) (string, bool) {
	rest := TrimStart(line)
	if rest == "" || rest[0] != '>' {
		return "", false
	}
	rest = rest[1:]
	if rest != "" && IsSpace(rest[0]) {
		rest = rest[1:]
	}
	return rest, true
}

// MatchListItem reads "- text", "* text" or "7. text": the marker followed by whitespace or the end of the line.
func MatchListItem(line string) (ListMarker, bool) {
	rest := TrimStart(line)
	item := ListMarker{Indent: Indentation(line)}
	length := 1
	if rest == "" || (rest[0] != '-' && rest[0] != '*') {
		digits := 0
		for digits < len(rest) && isDigit(rest[digits]) {
			digits++
		}
		if digits == 0 || digits > maxNumberDigits || digits == len(rest) || rest[digits] != '.' {
			return ListMarker{}, false
		}
		item.Ordered = true
		for i := 0; i < digits; i++ {
			item.Number = item.Number*10 + int(rest[i]-'0')
		}
		length = digits + 1
	}
	if length < len(rest) && !IsSpace(rest[length]) {
		return ListMarker{}, false
	}
	item.Text = Trim(rest[length:])
	return item, true
}
