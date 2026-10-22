package main

import (
	"strings"
	"testing"
)

func bullet(level int, text string) ListItem { return ListItem{Level: level, Text: text} }

func numbered(level, number int, text string) ListItem {
	return ListItem{Level: level, Ordered: true, Number: number, Text: text}
}

func renderItems(items ...ListItem) string {
	var out strings.Builder
	RenderList(items, &out)
	return out.String()
}

func checkList(t *testing.T, want, got string) {
	t.Helper()
	if got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestRendersAFlatList(t *testing.T) {
	checkList(t, "<ul>\n<li>a</li>\n<li><em>b</em></li>\n</ul>\n", renderItems(bullet(0, "a"), bullet(0, "*b*")))
}

func TestNestsDeeperItems(t *testing.T) {
	checkList(t, "<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n</li>\n<li>c</li>\n</ul>\n",
		renderItems(bullet(0, "a"), bullet(1, "b"), bullet(0, "c")))
}

func TestStartsOrderedListsAtTheirFirstNumber(t *testing.T) {
	checkList(t, "<ol start=\"3\">\n<li>x</li>\n<li>y</li>\n</ol>\n", renderItems(numbered(0, 3, "x"), numbered(0, 9, "y")))
	checkList(t, "<ol>\n<li>x</li>\n</ol>\n", renderItems(numbered(0, 1, "x")))
}

func TestStartsANewListWhereTheKindChanges(t *testing.T) {
	checkList(t, "<ul>\n<li>a</li>\n</ul>\n<ol>\n<li>b</li>\n</ol>\n", renderItems(bullet(0, "a"), numbered(0, 1, "b")))
	checkList(t, "<ul>\n<li>a\n<ol>\n<li>x</li>\n</ol>\n<ul>\n<li>y</li>\n</ul>\n</li>\n</ul>\n",
		renderItems(bullet(0, "a"), numbered(1, 1, "x"), bullet(1, "y")))
}

func TestNestsSkippedLevelsOnce(t *testing.T) {
	checkList(t, "<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n<ul>\n<li>c</li>\n</ul>\n</li>\n</ul>\n",
		renderItems(bullet(0, "a"), bullet(3, "b"), bullet(1, "c")))
}

func TestStartsAnotherListBelowTheFirstLevel(t *testing.T) {
	checkList(t, "<ul>\n<li>a</li>\n</ul>\n<ul>\n<li>b</li>\n</ul>\n", renderItems(bullet(2, "a"), bullet(0, "b")))
}

func TestMakesItemsFromMarkers(t *testing.T) {
	item := MakeListItem(ListMarker{Indent: 5, Ordered: true, Number: 12, Text: "text"})
	if item.Level != 2 || !item.Ordered || item.Number != 12 || item.Text != "text" {
		t.Errorf("got %+v", item)
	}
}
