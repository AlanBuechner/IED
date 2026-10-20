// The syntax tree of a Regex pattern (regex.ch) and the parser that builds it; regexcompile.cf turns the tree into
// the program the VM runs.

include "regex.ch"

namespace Core
{
	enum class RegexNodeKind { Empty, Literal, Class, Any, Concat, Alternate, Repeat, Group, Assert }

	struct RegexNode
	{
		public RegexNodeKind kind;
		public int32 value;      // Literal: the code point. Repeat: the least count. Group: its number, -1 for (?:).
		                         // Assert: the RegexOp. Any: 1 when it matches a line break too.
		public int32 most;       // Repeat: the most, -1 for no limit
		public bool greedy;      // Repeat
		public bool foldCase;    // Literal, Class: under (?i)
		public bool negated;     // Class
		public int32 first;      // Class: its first range. Concat, Alternate: its first child. Repeat, Group: the child.
		public int32 count;      // Class: its ranges. Concat, Alternate: its children.
	}

	class RegexParser
	{
		[operator("create")] public void Init(StringView pattern) { this.pattern = pattern; }

		// The whole pattern's tree: its root node, or -1 with `error` and `errorOffset`.
		public int32 Parse();

		public Array<RegexNode> nodes;
		public Array<int32> children;      // Concat and Alternate children, each node's together
		public Array<RegexRange> ranges;   // classes' ranges, as written
		public Array<String> names;        // names[g]: group g's name
		public int32 groups;
		public String error;
		public int64 errorOffset;

		private int32 Alternation();
		private int32 Concatenation();
		private int32 Repetition();
		private int32 Atom();
		private int32 Group();
		private int32 GroupName();
		private bool Flags(int64 start);
		private int32 Class();
		private bool ClassItem(Array<RegexRange>& into);
		private bool NamedClass(Array<RegexRange>& into);
		private int32 Escape();
		private int32 EscapedPoint(int64 start);
		private bool ShorthandClass(int32 letter, Array<RegexRange>& into);
		private int32 HexEscape(int64 start);
		private bool Count(int32& least, int32& most);
		private int32 Number();

		private int32 Add(RegexNode node);
		private int32 AddList(RegexNodeKind kind, const Array<int32>& items);
		private int32 AddClass(const Array<RegexRange>& items, bool negated);
		private int32 Fail(const char* why, int64 offset);
		private bool AtEnd() const { return at >= pattern.ByteLength(); }
		private int32 Peek() const { return AtEnd() ? -1 : pattern.ByteAt(at); }
		private int32 Next();   // the code point at `at`, moving past it

		private StringView pattern;
		private int64 at;
		private bool foldCase;
		private bool multiLine;
		private bool dotAll;
		private int32 depth;
	}

	// The most a counted repetition may say, and the most nested groups.
	const int32 kRegexMaxCount = 1000;
	const int32 kRegexMaxDepth = 250;
	const int32 kRegexMaxCodePoint = 0x10FFFF;
}
