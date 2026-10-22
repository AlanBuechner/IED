package main

import "strings"

// isPunctuation reports whether c is ASCII punctuation: what a backslash escapes.
func isPunctuation(c byte) bool {
	return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') || (c >= '{' && c <= '~')
}

// isInlineSpace is the whitespace that decides whether a run of '*' can open or close emphasis; '\n' joins a
// block's lines.
func isInlineSpace(c byte) bool { return c == ' ' || c == '\t' || c == '\n' }

func runLength(text string, at int, c byte) int {
	end := at
	for end < len(text) && text[end] == c {
		end++
	}
	return end - at
}

// findBacktickRun gives where the next run of exactly `length` backticks at or after `from` starts.
func findBacktickRun(text string, from, length int) (int, bool) {
	at := from
	for at < len(text) {
		if text[at] != '`' {
			at++
			continue
		}
		run := runLength(text, at, '`')
		if run == length {
			return at, true
		}
		at += run
	}
	return 0, false
}

// codeSpanText is a code span's content: line breaks become spaces, then one space comes off each end when both
// ends have one and the content is not all spaces.
func codeSpanText(raw string) string {
	text := strings.ReplaceAll(raw, "\n", " ")
	padded := len(text) >= 2 && text[0] == ' ' && text[len(text)-1] == ' '
	if padded && strings.Trim(text, " ") != "" {
		text = text[1 : len(text)-1]
	}
	return text
}

// findLabelEnd gives the ']' that closes the '[' at `open`: brackets nest, and escaped characters and code spans
// are skipped.
func findLabelEnd(text string, open int) (int, bool) {
	depth := 0
	at := open
	for at < len(text) {
		c := text[at]
		if c == '\\' {
			at += 2
			continue
		}
		if c == '`' {
			ticks := runLength(text, at, '`')
			if close, ok := findBacktickRun(text, at+ticks, ticks); ok {
				at = close + ticks
			} else {
				at += ticks
			}
			continue
		}
		if c == '[' {
			depth++
		}
		if c == ']' {
			depth--
			if depth == 0 {
				return at, true
			}
		}
		at++
	}
	return 0, false
}

type destination struct {
	url string // escapes resolved
	end int    // just past the ')'
}

// readDestination reads "(url)" at `at`: the url runs to the first unescaped ')' and holds no whitespace.
func readDestination(text string, at int) (destination, bool) {
	if at >= len(text) || text[at] != '(' {
		return destination{}, false
	}
	var url strings.Builder
	for i := at + 1; i < len(text); i++ {
		c := text[i]
		if c == ')' {
			return destination{url: url.String(), end: i + 1}, true
		}
		if isInlineSpace(c) {
			return destination{}, false
		}
		if c == '\\' && i+1 < len(text) && isPunctuation(text[i+1]) {
			i++
			c = text[i]
		}
		url.WriteByte(c)
	}
	return destination{}, false
}

// token is a piece of text on its way to HTML: finished HTML, or a run of '*' that may open or close emphasis.
type token struct {
	delimiter bool
	html      string // the finished HTML of a piece that is not a delimiter
	count     int    // the '*' of a delimiter not used by emphasis
	canOpen   bool   // followed by a character that is not whitespace
	canClose  bool   // preceded by a character that is not whitespace
	sealed    bool   // inside a matched pair, which nothing outside can reach
	closing   string // the tags it closes, written before its unused '*'
	opening   string // the tags it opens, written after them
}

type inlineParser struct {
	text         string
	linksAllowed bool
	tokens       []token
}

// RenderInline gives the HTML of a paragraph's, a heading's or a list item's text (lines joined with '\n').
func RenderInline(text string) string {
	parser := inlineParser{text: text, linksAllowed: true}
	return parser.render()
}

func (p *inlineParser) render() string {
	p.tokenize()
	p.matchEmphasis()
	var html strings.Builder
	for _, t := range p.tokens {
		if !t.delimiter {
			html.WriteString(t.html)
			continue
		}
		html.WriteString(t.closing)
		html.WriteString(strings.Repeat("*", t.count))
		html.WriteString(t.opening)
	}
	return html.String()
}

func (p *inlineParser) tokenize() {
	at := 0
	for at < len(p.text) {
		switch c := p.text[at]; {
		case c == '\\':
			at = p.readEscape(at)
		case c == '`':
			at = p.readCodeSpan(at)
		case c == '[' && p.linksAllowed:
			at = p.readLink(at)
		case c == '*':
			at = p.readDelimiter(at)
		default:
			p.appendText(p.text[at : at+1])
			at++
		}
	}
}

func (p *inlineParser) appendHTML(html string) {
	if len(p.tokens) == 0 || p.tokens[len(p.tokens)-1].delimiter {
		p.tokens = append(p.tokens, token{})
	}
	p.tokens[len(p.tokens)-1].html += html
}

func (p *inlineParser) appendText(text string) {
	var html strings.Builder
	AppendEscaped(&html, text)
	p.appendHTML(html.String())
}

// readEscape: "\*" is a literal '*'; a backslash before anything but ASCII punctuation is itself.
func (p *inlineParser) readEscape(at int) int {
	if at+1 < len(p.text) && isPunctuation(p.text[at+1]) {
		p.appendText(p.text[at+1 : at+2])
		return at + 2
	}
	p.appendText("\\")
	return at + 1
}

// readCodeSpan: a run of backticks opens a code span closed by the next run of the same length; without one it is
// text.
func (p *inlineParser) readCodeSpan(at int) int {
	ticks := runLength(p.text, at, '`')
	close, ok := findBacktickRun(p.text, at+ticks, ticks)
	if !ok {
		p.appendText(p.text[at : at+ticks])
		return at + ticks
	}
	p.appendHTML("<code>")
	p.appendText(codeSpanText(p.text[at+ticks : close]))
	p.appendHTML("</code>")
	return close + ticks
}

// readLink reads "[text](url)"; the text is parsed on its own, without links. Anything else leaves the '[' as text.
func (p *inlineParser) readLink(at int) int {
	close, found := findLabelEnd(p.text, at)
	var dest destination
	if found {
		dest, found = readDestination(p.text, close+1)
	}
	if !found {
		p.appendText("[")
		return at + 1
	}
	var html strings.Builder
	html.WriteString("<a href=\"")
	AppendEscaped(&html, dest.url)
	html.WriteString("\">")
	label := inlineParser{text: p.text[at+1 : close]}
	html.WriteString(label.render())
	html.WriteString("</a>")
	p.appendHTML(html.String())
	return dest.end
}

func (p *inlineParser) readDelimiter(at int) int {
	run := runLength(p.text, at, '*')
	p.tokens = append(p.tokens, token{
		delimiter: true,
		count:     run,
		canOpen:   at+run < len(p.text) && !isInlineSpace(p.text[at+run]),
		canClose:  at > 0 && !isInlineSpace(p.text[at-1]),
	})
	return at + run
}

// matchEmphasis pairs each closer, left to right, with the nearest opener before it while both have '*' left.
func (p *inlineParser) matchEmphasis() {
	for closer := range p.tokens {
		if !p.tokens[closer].delimiter || !p.tokens[closer].canClose {
			continue
		}
		for p.tokens[closer].count > 0 {
			opener, ok := p.findOpener(closer)
			if !ok {
				break
			}
			p.pair(opener, closer)
		}
	}
}

func (p *inlineParser) findOpener(closer int) (int, bool) {
	for i := closer - 1; i >= 0; i-- {
		t := &p.tokens[i]
		if t.delimiter && !t.sealed && t.canOpen && t.count > 0 {
			return i, true
		}
	}
	return 0, false
}

// pair: two '*' from each side make <strong> when both have two, else one makes <em>; what lies between is sealed.
func (p *inlineParser) pair(opener, closer int) {
	open, close := &p.tokens[opener], &p.tokens[closer]
	used, tag := 1, "em"
	if open.count >= 2 && close.count >= 2 {
		used, tag = 2, "strong"
	}
	open.count -= used
	close.count -= used
	open.opening = "<" + tag + ">" + open.opening
	close.closing += "</" + tag + ">"
	for i := opener + 1; i < closer; i++ {
		p.tokens[i].sealed = true
	}
}
