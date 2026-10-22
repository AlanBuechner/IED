#include "text.h"
#include "check.h"

using namespace mdhtml;

TEST(SplitsLinesAndDropsCarriageReturns) {
    std::vector<std::string_view> lines = SplitLines("a\r\nb\n\nc");
    CHECK(lines.size() == 4);
    CHECK_EQ("a", lines[0]);
    CHECK_EQ("b", lines[1]);
    CHECK_EQ("", lines[2]);
    CHECK_EQ("c", lines[3]);
}

TEST(AFinalNewlineStartsNoLine) {
    CHECK(SplitLines("a\n").size() == 1);
    CHECK(SplitLines("").empty());
    CHECK(SplitLines("\n").size() == 1);
    CHECK(SplitLines("a\n\n").size() == 2);
}

TEST(TrimsSpacesAndTabsOnly) {
    CHECK_EQ("a b", Trim(" \t a b\t "));
    CHECK_EQ("x  ", TrimStart("\t x  "));
    CHECK_EQ("\n", Trim("\n"));
    CHECK(IsBlank(" \t "));
    CHECK(IsBlank(""));
    CHECK(!IsBlank("  ."));
}

TEST(CountsIndentation) {
    CHECK(Indentation("    x") == 4);
    CHECK(Indentation("\t x") == 2);
    CHECK(Indentation("x  ") == 0);
}

TEST(EscapesHtml) {
    std::string out;
    AppendEscaped(out, "a & b < c > d \"e\" 'f'");
    CHECK_EQ("a &amp; b &lt; c &gt; d &quot;e&quot; 'f'", out);
}
