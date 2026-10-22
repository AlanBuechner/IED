// Lists: the items of consecutive list lines, nested by indentation, as HTML.
include "lines.ch"

namespace MdHtml
{
	struct ListItem
	{
		public int32 level;         // the indentation in steps of two spaces
		public bool ordered;
		public int32 number;        // the first item of an ordered list gives its start
		public Core::String text;   // the item's line and its continuation lines, joined with '\n'
	}

	// The item a list line starts.
	ListItem MakeListItem(const ListMarker& marker);
	// Appends the HTML of a run of items: an item deeper than the one before opens a list inside it, one shallower
	// closes the lists deeper than itself, and an item of the other kind (ordered or not) on the same level closes
	// that level's list and opens a new one.
	void RenderList(const Core::Array<ListItem>& items, Core::StringBuilder& out);
}
