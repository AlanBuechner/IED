#include "inline.h"
#include "check.h"

using namespace mdhtml;

TEST(RendersEmphasisAndStrong) {
    CHECK_EQ("<em>em</em> and <strong>strong</strong>", RenderInline("*em* and **strong**"));
    CHECK_EQ("<em><strong>both</strong></em>", RenderInline("***both***"));
    CHECK_EQ("2<em>3</em>4", RenderInline("2*3*4"));
    CHECK_EQ("<em>a\nb</em>", RenderInline("*a\nb*"));
}

TEST(NestsEmphasis) {
    CHECK_EQ("<em>a <strong>b</strong> c</em>", RenderInline("*a **b** c*"));
    CHECK_EQ("<strong>a <em>b</em> c</strong>", RenderInline("**a *b* c**"));
    CHECK_EQ("<em>a</em><em>b</em>", RenderInline("*a**b*"));
}

TEST(LeavesUnmatchedStarsAsText) {
    CHECK_EQ("*open", RenderInline("*open"));
    CHECK_EQ("close*", RenderInline("close*"));
    CHECK_EQ("*<em>a</em>", RenderInline("**a*"));
    CHECK_EQ("<em>a</em>*", RenderInline("*a**"));
    CHECK_EQ("a * b", RenderInline("a * b"));
    CHECK_EQ("* not em *", RenderInline("* not em *"));
    CHECK_EQ("a *\nb*", RenderInline("a *\nb*"));
}

TEST(RendersCodeSpans) {
    CHECK_EQ("<code>code</code>", RenderInline("`code`"));
    CHECK_EQ("<code>a ` b</code>", RenderInline("``a ` b``"));
    CHECK_EQ("<code>a</code>", RenderInline("` a `"));
    CHECK_EQ("<code>  </code>", RenderInline("`  `"));
    CHECK_EQ("<code>a b</code>", RenderInline("`a\nb`"));
    CHECK_EQ("<code>*not em* \\*</code>", RenderInline("`*not em* \\*`"));
    CHECK_EQ("<code>&lt;tag&gt;</code>", RenderInline("`<tag>`"));
}

TEST(LeavesUnclosedBackticksAsText) {
    CHECK_EQ("`unclosed", RenderInline("`unclosed"));
    CHECK_EQ("```a``", RenderInline("```a``"));
}

TEST(RendersLinks) {
    CHECK_EQ("<a href=\"http://x.com\">text</a>", RenderInline("[text](http://x.com)"));
    CHECK_EQ("<a href=\"u\"><em>em</em> text</a>", RenderInline("[*em* text](u)"));
    CHECK_EQ("<a href=\"u\">a [b] c</a>", RenderInline("[a [b] c](u)"));
    CHECK_EQ("<a href=\"u\"><code>]</code></a>", RenderInline("[`]`](u)"));
    CHECK_EQ("<a href=\"\">a</a>", RenderInline("[a]()"));
}

TEST(LinksDoNotNest) {
    CHECK_EQ("<a href=\"o\">outer [inner](i)</a>", RenderInline("[outer [inner](i)](o)"));
    CHECK_EQ("*<a href=\"u\">a*</a>", RenderInline("*[a*](u)"));
}

TEST(EscapesLinkDestinations) {
    CHECK_EQ("<a href=\"u)v\">a</a>", RenderInline("[a](u\\)v)"));
    CHECK_EQ("<a href=\"x?a=1&amp;b=&quot;2&quot;\">a</a>", RenderInline("[a](x?a=1&b=\"2\")"));
    CHECK_EQ("<a href=\"&lt;b&gt;\">a</a>", RenderInline("[a](<b>)"));
}

TEST(LeavesWhatIsNoLinkAsText) {
    CHECK_EQ("[no url]", RenderInline("[no url]"));
    CHECK_EQ("[a](b c)", RenderInline("[a](b c)"));
    CHECK_EQ("[a] (u)", RenderInline("[a] (u)"));
    CHECK_EQ("[a](u", RenderInline("[a](u"));
}

TEST(AppliesBackslashEscapes) {
    CHECK_EQ("*not*", RenderInline("\\*not\\*"));
    CHECK_EQ("\\a", RenderInline("\\a"));
    CHECK_EQ("a\\", RenderInline("a\\"));
    CHECK_EQ("\\", RenderInline("\\\\"));
    CHECK_EQ("`code`", RenderInline("\\`code\\`"));
    CHECK_EQ("[a](b)", RenderInline("\\[a](b)"));
    CHECK_EQ("&lt;b&gt;", RenderInline("\\<b\\>"));
}

TEST(EscapesHtmlInText) {
    CHECK_EQ("a &lt; b &amp; c &gt; &quot;d&quot;", RenderInline("a < b & c > \"d\""));
    CHECK_EQ("&lt;b&gt;bold&lt;/b&gt;", RenderInline("<b>bold</b>"));
    CHECK_EQ("&amp;amp;", RenderInline("&amp;"));
}
