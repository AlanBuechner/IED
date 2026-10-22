#include "lines.h"
#include "check.h"

using namespace mdhtml;

TEST(ClassifiesLines) {
    CHECK(Classify("   ") == LineKind::Blank);
    CHECK(Classify("```go") == LineKind::Fence);
    CHECK(Classify("### Title") == LineKind::Heading);
    CHECK(Classify("- - -") == LineKind::Rule);
    CHECK(Classify("  > quoted") == LineKind::Quote);
    CHECK(Classify("12. item") == LineKind::ListItem);
    CHECK(Classify("plain text") == LineKind::Text);
}

TEST(ReadsFences) {
    std::optional<Fence> fence = MatchFence("  ````python extra words");
    CHECK(fence.has_value());
    CHECK(fence->ticks == 4);
    CHECK_EQ("python", fence->language);
    CHECK(MatchFence("```")->language.empty());
    CHECK(!MatchFence("``"));
    CHECK(!MatchFence("```a`b"));
}

TEST(ClosesFencesWithEnoughTicks) {
    CHECK(ClosesFence("````", 3));
    CHECK(ClosesFence("  ```  ", 3));
    CHECK(!ClosesFence("```", 4));
    CHECK(!ClosesFence("``` x", 3));
}

TEST(ReadsHeadings) {
    std::optional<Heading> heading = MatchHeading("##   Title  ");
    CHECK(heading.has_value());
    CHECK(heading->level == 2);
    CHECK_EQ("Title", heading->text);
    CHECK(MatchHeading("######")->level == 6);
    CHECK(!MatchHeading("####### seven"));
    CHECK(!MatchHeading("#hashtag"));
}

TEST(StripsClosingHashes) {
    CHECK_EQ("Title", MatchHeading("# Title ###")->text);
    CHECK_EQ("C#", MatchHeading("# C#")->text);
    CHECK_EQ("", MatchHeading("# ###")->text);
    CHECK_EQ("a \\#", MatchHeading("# a \\#")->text);
}

TEST(ReadsRules) {
    CHECK(IsRule("---"));
    CHECK(IsRule(" * * * * "));
    CHECK(!IsRule("--"));
    CHECK(!IsRule("-*-"));
    CHECK(!IsRule("--- x"));
}

TEST(ReadsQuotes) {
    CHECK_EQ("text", *MatchQuote("> text"));
    CHECK_EQ("text", *MatchQuote(">text"));
    CHECK_EQ(" two", *MatchQuote(">  two"));
    CHECK_EQ("> nested", *MatchQuote("> > nested"));
    CHECK(!MatchQuote("quote >"));
}

TEST(ReadsListMarkers) {
    std::optional<ListMarker> bullet = MatchListItem("    *   star ");
    CHECK(bullet.has_value());
    CHECK(bullet->indent == 4);
    CHECK(!bullet->ordered);
    CHECK_EQ("star", bullet->text);
    std::optional<ListMarker> numbered = MatchListItem("007. bond");
    CHECK(numbered.has_value());
    CHECK(numbered->ordered);
    CHECK(numbered->number == 7);
    CHECK(MatchListItem("-")->text.empty());
}

TEST(RejectsWhatIsNoListMarker) {
    CHECK(!MatchListItem("-item"));
    CHECK(!MatchListItem("**bold**"));
    CHECK(!MatchListItem("1.5 pounds"));
    CHECK(!MatchListItem("1) one"));
    CHECK(!MatchListItem("1234567890. ten digits"));
}
