package main

import (
	"strconv"
	"strings"
)

const spacesPerLevel = 2

// ListItem is one item of a list: its level (the indentation in steps of two spaces), its kind, its number (the
// first item of an ordered list gives its start), and its line and continuation lines joined with '\n'.
type ListItem struct {
	Level   int
	Ordered bool
	Number  int
	Text    string
}

// openList is a list still open while items are written, with its last item still open too.
type openList struct {
	level       int
	ordered     bool
	itemHasList bool // the open item already holds a nested list
}

// MakeListItem gives the item a list line starts.
func MakeListItem(marker ListMarker) ListItem {
	return ListItem{Level: marker.Indent / spacesPerLevel, Ordered: marker.Ordered, Number: marker.Number, Text: marker.Text}
}

func openingTag(item ListItem) string {
	if !item.Ordered {
		return "<ul>\n"
	}
	if item.Number == 1 {
		return "<ol>\n"
	}
	return "<ol start=\"" + strconv.Itoa(item.Number) + "\">\n"
}

func closeList(open []openList, out *strings.Builder) []openList {
	if open[len(open)-1].ordered {
		out.WriteString("</li>\n</ol>\n")
	} else {
		out.WriteString("</li>\n</ul>\n")
	}
	return open[:len(open)-1]
}

// RenderList appends the HTML of a run of items: an item deeper than the one before opens a list inside it, one
// shallower closes the lists deeper than itself, and an item of the other kind (ordered or not) on the same level
// closes that level's list and opens a new one.
func RenderList(items []ListItem, out *strings.Builder) {
	var open []openList
	for _, item := range items {
		for len(open) > 0 && open[len(open)-1].level > item.Level {
			open = closeList(open, out)
		}
		if len(open) > 0 && open[len(open)-1].level == item.Level && open[len(open)-1].ordered != item.Ordered {
			open = closeList(open, out)
		}
		if len(open) > 0 && open[len(open)-1].level == item.Level {
			out.WriteString("</li>\n")
			open[len(open)-1].itemHasList = false
		} else {
			if len(open) > 0 && !open[len(open)-1].itemHasList {
				out.WriteString("\n")
				open[len(open)-1].itemHasList = true
			}
			out.WriteString(openingTag(item))
			open = append(open, openList{level: item.Level, ordered: item.Ordered})
		}
		out.WriteString("<li>")
		out.WriteString(RenderInline(item.Text))
	}
	for len(open) > 0 {
		open = closeList(open, out)
	}
}
