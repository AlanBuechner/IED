#include "document.h"
#include "check.h"

using namespace mdhtml;

TEST(RendersNothingForAnEmptyDocument) {
    CHECK_EQ("", RenderDocument(""));
    CHECK_EQ("", RenderDocument("\n  \n\t\n"));
}

TEST(JoinsParagraphLines) {
    CHECK_EQ("<p>a\nb</p>\n<p>c</p>\n", RenderDocument("a\nb\n\nc"));
    CHECK_EQ("<p>a\nb</p>\n", RenderDocument("  a  \r\n  b"));
}

TEST(RendersHeadings) {
    CHECK_EQ("<h1>One</h1>\n<h2>Two</h2>\n<p>####### seven</p>\n", RenderDocument("# One\n## Two ##\n####### seven"));
}

TEST(EndsParagraphsAtOtherBlocks) {
    CHECK_EQ("<p>text</p>\n<h1>Head</h1>\n<p>more</p>\n", RenderDocument("text\n# Head\nmore"));
    CHECK_EQ("<p>text</p>\n<hr />\n", RenderDocument("text\n---"));
    CHECK_EQ("<p>text</p>\n<ul>\n<li>item</li>\n</ul>\n", RenderDocument("text\n- item"));
}

TEST(RendersCodeBlocks) {
    CHECK_EQ("<pre><code class=\"language-js\">if (a &lt; b) {}\n</code></pre>\n", RenderDocument("```js\nif (a < b) {}\n```"));
    CHECK_EQ("<pre><code></code></pre>\n", RenderDocument("```\n```"));
}

TEST(RunsAnUnclosedFenceToTheEnd) {
    CHECK_EQ("<pre><code>  keep\n\n*x*\n</code></pre>\n", RenderDocument("```\n  keep\n\n*x*"));
}

TEST(ParsesQuoteContentAsBlocks) {
    CHECK_EQ("<blockquote>\n<p>a\nb</p>\n<p>c</p>\n</blockquote>\n", RenderDocument("> a\n> b\n>\n> c"));
    CHECK_EQ("<blockquote>\n<p>a</p>\n<blockquote>\n<p>b</p>\n</blockquote>\n</blockquote>\n", RenderDocument("> a\n>> b"));
    CHECK_EQ("<blockquote>\n<pre><code>x\n</code></pre>\n</blockquote>\n<p>after</p>\n", RenderDocument("> ```\n> x\n> ```\nafter"));
}

TEST(EndsAQuoteAtALineWithoutMarker) {
    CHECK_EQ("<blockquote>\n<p>a</p>\n</blockquote>\n<p>b</p>\n", RenderDocument("> a\nb"));
}

TEST(ContinuesListItemsAndListsAcrossBlankLines) {
    CHECK_EQ("<ul>\n<li>a\ncontinued</li>\n<li>b</li>\n</ul>\n<p>after</p>\n", RenderDocument("- a\ncontinued\n\n- b\n\nafter"));
}

TEST(NestsListsByIndentation) {
    CHECK_EQ("<ol>\n<li>one\n<ul>\n<li>sub</li>\n</ul>\n</li>\n<li>two</li>\n</ol>\n", RenderDocument("1. one\n   - sub\n2. two"));
    CHECK_EQ("<blockquote>\n<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n</li>\n</ul>\n</blockquote>\n", RenderDocument("> - a\n>   - b"));
}

TEST(PrefersARuleToAListItem) {
    CHECK_EQ("<hr />\n<ul>\n<li></li>\n</ul>\n", RenderDocument("- - -\n-"));
}
