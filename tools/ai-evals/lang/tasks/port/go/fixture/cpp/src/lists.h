// Lists: the items of consecutive list lines, nested by indentation, as HTML.
#pragma once

#include <string>
#include <vector>

#include "lines.h"

namespace mdhtml {

struct ListItem {
    int level;         // the indentation in steps of two spaces
    bool ordered;
    int number;        // the first item of an ordered list gives its start
    std::string text;  // the item's line and its continuation lines, joined with '\n'
};

// The item a list line starts.
ListItem MakeListItem(const ListMarker& marker);

// Appends the HTML of a run of items: an item deeper than the one before opens a list inside it, one shallower
// closes the lists deeper than itself, and an item of the other kind (ordered or not) on the same level closes
// that level's list and opens a new one.
void RenderList(const std::vector<ListItem>& items, std::string& out);

}  // namespace mdhtml
