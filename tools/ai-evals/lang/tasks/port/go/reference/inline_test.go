package main

import "testing"

func checkInline(t *testing.T, cases [][2]string) {
	t.Helper()
	for _, c := range cases {
		if got := RenderInline(c[0]); got != c[1] {
			t.Errorf("%q: got %q, want %q", c[0], got, c[1])
		}
	}
}

func TestRendersEmphasisAndStrong(t *testing.T) {
	checkInline(t, [][2]string{
		{"*em* and **strong**", "<em>em</em> and <strong>strong</strong>"},
		{"***both***", "<em><strong>both</strong></em>"},
		{"2*3*4", "2<em>3</em>4"},
		{"*a\nb*", "<em>a\nb</em>"},
	})
}

func TestNestsEmphasis(t *testing.T) {
	checkInline(t, [][2]string{
		{"*a **b** c*", "<em>a <strong>b</strong> c</em>"},
		{"**a *b* c**", "<strong>a <em>b</em> c</strong>"},
		{"*a**b*", "<em>a</em><em>b</em>"},
	})
}

func TestLeavesUnmatchedStarsAsText(t *testing.T) {
	checkInline(t, [][2]string{
		{"*open", "*open"},
		{"close*", "close*"},
		{"**a*", "*<em>a</em>"},
		{"*a**", "<em>a</em>*"},
		{"a * b", "a * b"},
		{"* not em *", "* not em *"},
		{"a *\nb*", "a *\nb*"},
	})
}

func TestRendersCodeSpans(t *testing.T) {
	checkInline(t, [][2]string{
		{"`code`", "<code>code</code>"},
		{"``a ` b``", "<code>a ` b</code>"},
		{"` a `", "<code>a</code>"},
		{"`  `", "<code>  </code>"},
		{"`a\nb`", "<code>a b</code>"},
		{"`*not em* \\*`", "<code>*not em* \\*</code>"},
		{"`<tag>`", "<code>&lt;tag&gt;</code>"},
	})
}

func TestLeavesUnclosedBackticksAsText(t *testing.T) {
	checkInline(t, [][2]string{
		{"`unclosed", "`unclosed"},
		{"```a``", "```a``"},
	})
}

func TestRendersLinks(t *testing.T) {
	checkInline(t, [][2]string{
		{"[text](http://x.com)", "<a href=\"http://x.com\">text</a>"},
		{"[*em* text](u)", "<a href=\"u\"><em>em</em> text</a>"},
		{"[a [b] c](u)", "<a href=\"u\">a [b] c</a>"},
		{"[`]`](u)", "<a href=\"u\"><code>]</code></a>"},
		{"[a]()", "<a href=\"\">a</a>"},
	})
}

func TestLinksDoNotNest(t *testing.T) {
	checkInline(t, [][2]string{
		{"[outer [inner](i)](o)", "<a href=\"o\">outer [inner](i)</a>"},
		{"*[a*](u)", "*<a href=\"u\">a*</a>"},
	})
}

func TestEscapesLinkDestinations(t *testing.T) {
	checkInline(t, [][2]string{
		{"[a](u\\)v)", "<a href=\"u)v\">a</a>"},
		{"[a](x?a=1&b=\"2\")", "<a href=\"x?a=1&amp;b=&quot;2&quot;\">a</a>"},
		{"[a](<b>)", "<a href=\"&lt;b&gt;\">a</a>"},
	})
}

func TestLeavesWhatIsNoLinkAsText(t *testing.T) {
	checkInline(t, [][2]string{
		{"[no url]", "[no url]"},
		{"[a](b c)", "[a](b c)"},
		{"[a] (u)", "[a] (u)"},
		{"[a](u", "[a](u"},
	})
}

func TestAppliesBackslashEscapes(t *testing.T) {
	checkInline(t, [][2]string{
		{"\\*not\\*", "*not*"},
		{"\\a", "\\a"},
		{"a\\", "a\\"},
		{"\\\\", "\\"},
		{"\\`code\\`", "`code`"},
		{"\\[a](b)", "[a](b)"},
		{"\\<b\\>", "&lt;b&gt;"},
	})
}

func TestEscapesHTMLInText(t *testing.T) {
	checkInline(t, [][2]string{
		{"a < b & c > \"d\"", "a &lt; b &amp; c &gt; &quot;d&quot;"},
		{"<b>bold</b>", "&lt;b&gt;bold&lt;/b&gt;"},
		{"&amp;", "&amp;amp;"},
	})
}
