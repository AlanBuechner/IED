package main

import (
	"strconv"
	"strings"
)

// blockParser walks a document's lines and writes each block: headings, paragraphs, code blocks, quotes, rules
// and lists.
type blockParser struct {
	lines []string
	out   *strings.Builder
	pos   int
}

// RenderDocument gives the HTML of a whole Markdown document.
func RenderDocument(text string) string {
	var out strings.Builder
	RenderBlocks(SplitLines(text), &out)
	return out.String()
}

// RenderBlocks appends the HTML of the blocks these lines make: a document's lines, or the content of a quote's.
func RenderBlocks(lines []string, out *strings.Builder) {
	parser := blockParser{lines: lines, out: out}
	parser.run()
}

func (p *blockParser) run() {
	for p.pos < len(p.lines) {
		switch Classify(p.lines[p.pos]) {
		case LineBlank:
			p.pos++
		case LineFence:
			p.writeCodeBlock()
		case LineHeading:
			p.writeHeading()
		case LineRule:
			p.writeRule()
		case LineQuote:
			p.writeQuote()
		case LineListItem:
			p.writeList()
		case LineText:
			p.writeParagraph()
		}
	}
}

// writeCodeBlock writes the lines up to a closing fence, or to the end of the lines when none comes, kept as they
// are.
func (p *blockParser) writeCodeBlock() {
	fence, _ := MatchFence(p.lines[p.pos])
	p.pos++
	p.out.WriteString("<pre><code")
	if fence.Language != "" {
		p.out.WriteString(" class=\"language-")
		AppendEscaped(p.out, fence.Language)
		p.out.WriteString("\"")
	}
	p.out.WriteString(">")
	for p.pos < len(p.lines) && !ClosesFence(p.lines[p.pos], fence.Ticks) {
		AppendEscaped(p.out, p.lines[p.pos])
		p.out.WriteString("\n")
		p.pos++
	}
	if p.pos < len(p.lines) {
		p.pos++
	}
	p.out.WriteString("</code></pre>\n")
}

func (p *blockParser) writeHeading() {
	heading, _ := MatchHeading(p.lines[p.pos])
	p.pos++
	tag := "h" + strconv.Itoa(heading.Level)
	p.out.WriteString("<" + tag + ">" + RenderInline(heading.Text) + "</" + tag + ">\n")
}

func (p *blockParser) writeRule() {
	p.out.WriteString("<hr />\n")
	p.pos++
}

// writeQuote takes consecutive quote lines; their content is parsed as blocks of its own.
func (p *blockParser) writeQuote() {
	var content []string
	for p.pos < len(p.lines) {
		inner, ok := MatchQuote(p.lines[p.pos])
		if !ok {
			break
		}
		content = append(content, inner)
		p.pos++
	}
	p.out.WriteString("<blockquote>\n")
	RenderBlocks(content, p.out)
	p.out.WriteString("</blockquote>\n")
}

// writeList takes list lines and the text lines that continue their items; blank lines only between two list
// lines.
func (p *blockParser) writeList() {
	var items []ListItem
	for p.pos < len(p.lines) {
		kind := Classify(p.lines[p.pos])
		if kind == LineListItem {
			marker, _ := MatchListItem(p.lines[p.pos])
			items = append(items, MakeListItem(marker))
		} else if kind == LineText {
			items[len(items)-1].Text += "\n" + Trim(p.lines[p.pos])
		} else if kind != LineBlank || !p.listResumesAfter(p.pos) {
			break
		}
		p.pos++
	}
	RenderList(items, p.out)
}

func (p *blockParser) listResumesAfter(blank int) bool {
	next := blank
	for next < len(p.lines) && IsBlank(p.lines[next]) {
		next++
	}
	return next < len(p.lines) && Classify(p.lines[next]) == LineListItem
}

// writeParagraph takes consecutive text lines, trimmed and joined with '\n'; any other kind of line ends the
// paragraph.
func (p *blockParser) writeParagraph() {
	text := Trim(p.lines[p.pos])
	p.pos++
	for p.pos < len(p.lines) && Classify(p.lines[p.pos]) == LineText {
		text += "\n" + Trim(p.lines[p.pos])
		p.pos++
	}
	p.out.WriteString("<p>" + RenderInline(text) + "</p>\n")
}
