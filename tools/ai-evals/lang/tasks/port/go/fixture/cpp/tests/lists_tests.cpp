#include "lists.h"
#include "check.h"

using namespace mdhtml;

namespace {

ListItem Bullet(int level, const char* text) { return ListItem{level, false, 0, text}; }
ListItem Numbered(int level, int number, const char* text) { return ListItem{level, true, number, text}; }

std::string Render(const std::vector<ListItem>& items) {
    std::string out;
    RenderList(items, out);
    return out;
}

}  // namespace

TEST(RendersAFlatList) {
    CHECK_EQ("<ul>\n<li>a</li>\n<li><em>b</em></li>\n</ul>\n", Render({Bullet(0, "a"), Bullet(0, "*b*")}));
}

TEST(NestsDeeperItems) {
    CHECK_EQ("<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n</li>\n<li>c</li>\n</ul>\n",
             Render({Bullet(0, "a"), Bullet(1, "b"), Bullet(0, "c")}));
}

TEST(StartsOrderedListsAtTheirFirstNumber) {
    CHECK_EQ("<ol start=\"3\">\n<li>x</li>\n<li>y</li>\n</ol>\n", Render({Numbered(0, 3, "x"), Numbered(0, 9, "y")}));
    CHECK_EQ("<ol>\n<li>x</li>\n</ol>\n", Render({Numbered(0, 1, "x")}));
}

TEST(StartsANewListWhereTheKindChanges) {
    CHECK_EQ("<ul>\n<li>a</li>\n</ul>\n<ol>\n<li>b</li>\n</ol>\n", Render({Bullet(0, "a"), Numbered(0, 1, "b")}));
    CHECK_EQ("<ul>\n<li>a\n<ol>\n<li>x</li>\n</ol>\n<ul>\n<li>y</li>\n</ul>\n</li>\n</ul>\n",
             Render({Bullet(0, "a"), Numbered(1, 1, "x"), Bullet(1, "y")}));
}

TEST(NestsSkippedLevelsOnce) {
    CHECK_EQ("<ul>\n<li>a\n<ul>\n<li>b</li>\n</ul>\n<ul>\n<li>c</li>\n</ul>\n</li>\n</ul>\n",
             Render({Bullet(0, "a"), Bullet(3, "b"), Bullet(1, "c")}));
}

TEST(StartsAnotherListBelowTheFirstLevel) {
    CHECK_EQ("<ul>\n<li>a</li>\n</ul>\n<ul>\n<li>b</li>\n</ul>\n", Render({Bullet(2, "a"), Bullet(0, "b")}));
}

TEST(MakesItemsFromMarkers) {
    ListItem item = MakeListItem(ListMarker{5, true, 12, "text"});
    CHECK(item.level == 2);
    CHECK(item.ordered);
    CHECK(item.number == 12);
    CHECK_EQ("text", item.text);
}
