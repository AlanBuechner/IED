// What one line of Markdown is, told by its first characters: the vocabulary of the block parser.
include "bedrock.ch"

namespace MdHtml
{
	enum class LineKind { Blank, Fence, Heading, Rule, Quote, ListItem, Text }

	struct Heading
	{
		public int32 level;             // 1 to 6
		public Core::StringView text;   // without the marker and a closing run of '#'
	}

	struct Fence
	{
		public int32 ticks;                 // the length of the opening backtick run, 3 or more
		public Core::StringView language;   // the first word of the info string; empty when there is none
	}

	struct ListMarker
	{
		public int32 indent;            // leading spaces and tabs
		public bool ordered;            // "12." rather than "-" or "*"
		public int32 number;            // an ordered item's number
		public Core::StringView text;   // what follows the marker, trimmed
	}

	// The kind of a line, tested in this order: blank, fence, heading, rule, quote, list item; anything else is text.
	LineKind Classify(Core::StringView line);
	// "```lang": three or more backticks; an info string with a backtick in it makes the line no fence.
	bool MatchFence(Core::StringView line, Fence& fence);
	// A run of at least `ticks` backticks with nothing but whitespace after it.
	bool ClosesFence(Core::StringView line, int32 ticks);
	// "## Title": one to six '#' followed by whitespace or the end of the line.
	bool MatchHeading(Core::StringView line, Heading& heading);
	// Three or more '-', or three or more '*', and nothing else but whitespace.
	bool IsRule(Core::StringView line);
	// "> text": what follows the '>' and the one space or tab after it.
	bool MatchQuote(Core::StringView line, Core::StringView& content);
	// "- text", "* text" or "7. text": the marker followed by whitespace or the end of the line.
	bool MatchListItem(Core::StringView line, ListMarker& marker);
}
