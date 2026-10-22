package main

import "testing"

func checkDocument(t *testing.T, cases [][2]string) {
	t.Helper()
	for _, c := range cases {
		if got := RenderDocument(c[0]); got != c[1] {
			t.Errorf("%q: got %q, want %q", c[0], got, c[1])
		}
	}
}

func TestRendersNothingForAnEmptyDocument(t *testing.T) {
	checkDocument(t, [][2]string{{"", ""}, {"\n  \n\t\n", ""}})
}

func TestJoinsParagraphLines(t *testing.T) {
	checkDocument(t, [][2]string{
		{"a\nb\n\nc", "<p>a\nb</p>\n<p>c</p>\n"},
		{"  a  \r\n  b", "<p>a\nb</p>\n"},
	})
}

func TestRendersHeadings(t *testing.T) {
	checkDocument(t, [][2]string{
		{"# One\n## Two ##\n####### seven", "<h1>One</h1>\n<h2>Two</h2>\n<p>####### seven</p>\n"},
	})
}

func TestEndsParagraphsAtOtherBlocks(t *testing.T) {
	checkDocument(t, [][2]string{
		{"text\n# Head\nmore", "<p>text</p>\n<h1>Head</h1>\n<p>more</p>\n"},
		{"text\n---", "<p>text</p>\n<hr />\n"},
		{"text\n- item", "<p>text</p>\n<ul>\n<li>item</li>\n</ul>\n"},
	})
}

func TestRendersCodeBlocks(t *testing.T) {
	checkDocument(t, [][2]string{
		{"```js\nif (a < b) {}\n```", "<pre><code class=\"language-js\">if (a &lt; b) {}\n</code></pre>\n"},
		{"```\n```", "<pre><code></code></pre>\n"},
	})
}

func TestRunsAnUnclosedFenceToTheEnd(t *testing.T) {
	checkDocument(t, [][2]string{{"```\n  keep\n\n*x*", "<pre><code>  keep\n\n*x*\n</code></pre>\n"}})
}

func TestParsesQuoteContentAsBlocks(t *testing.T) {
	checkDocument(t, [][2]string{
		{"> a\n> b\n>\n> c", "<blockquote>\n<p>a\nb</p>\n<p>c</p>\n</blockquote>\n"},
		{"> a\n>> b", "<blockquote>\n<p>a</p>\n<blockquote>\n<p>b</p>\n</blockquote>\n</blockquote>\n"},
		{"> ```\n> x\n> ```\nafter", "<blockquote>\n<pre><code>x\n</code></pre>\n</blockquote>\n<p>after</p>\n"},
	})
}

func TestEndsAQuoteAtALineWithoutMarker(t *testing.T) {
	checkDocument(t, [][2]string{{"> a\nb", "<blockquote>\n<p>a</p>\n</blockquote>\n<p>b</p>\n"}})
}

func TestContinuesListItemsAndListsAcrossBlankLines(t *testing.T) {
	checkDocument(t, [][2]string{
		{"- a\ncontinued\n\n- b\n\nafter", "<ul>\n<li>a\ncontinued</li>\n<li>b</li>\n</ul>\n<p>after</p>\n"},
	})
}

func TestNestsListsByIndentation(t *testing.T) {
	checkDocument(t, [][2]string{
		{"1. one\n   - sub\n2. two", "<ol>\n<li>one\n<ul>\n<li>sub</li>\n</ul>\n</li>\n<li>two</li>\n</ol>\n"},
		{"> - a\n>   - b", "<blockquote>\n<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n</li>\n</ul>\n</blockquote>\n"},
	})
}

func TestPrefersARuleToAListItem(t *testing.T) {
	checkDocument(t, [][2]string{{"- - -\n-", "<hr />\n<ul>\n<li></li>\n</ul>\n"}})
}
