#include "lists.h"

#include "inline.h"

namespace mdhtml {
namespace {

constexpr int kSpacesPerLevel = 2;

// A list still open while items are written, with its last item still open too.
struct OpenList {
    int level;
    bool ordered;
    bool itemHasList;  // the open item already holds a nested list
};

std::string OpeningTag(const ListItem& item) {
    if (!item.ordered) return "<ul>\n";
    if (item.number == 1) return "<ol>\n";
    return "<ol start=\"" + std::to_string(item.number) + "\">\n";
}

void CloseList(std::vector<OpenList>& open, std::string& out) {
    out += open.back().ordered ? "</li>\n</ol>\n" : "</li>\n</ul>\n";
    open.pop_back();
}

}  // namespace

ListItem MakeListItem(const ListMarker& marker) {
    return ListItem{marker.indent / kSpacesPerLevel, marker.ordered, marker.number, std::string(marker.text)};
}

void RenderList(const std::vector<ListItem>& items, std::string& out) {
    std::vector<OpenList> open;
    for (const ListItem& item : items) {
        while (!open.empty() && open.back().level > item.level) CloseList(open, out);
        if (!open.empty() && open.back().level == item.level && open.back().ordered != item.ordered) CloseList(open, out);
        if (!open.empty() && open.back().level == item.level) {
            out += "</li>\n";
            open.back().itemHasList = false;
        } else {
            if (!open.empty() && !open.back().itemHasList) {
                out += "\n";
                open.back().itemHasList = true;
            }
            out += OpeningTag(item);
            open.push_back(OpenList{item.level, item.ordered, false});
        }
        out += "<li>";
        out += RenderInline(item.text);
    }
    while (!open.empty()) CloseList(open, out);
}

}  // namespace mdhtml
