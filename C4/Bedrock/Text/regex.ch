// Regex: a regular expression compiled once, then matched in time linear in the text (D-71). It runs as a Pike VM,
// so no pattern makes a match take exponential time; the price is no backreferences and no lookaround. The syntax is
// RE2's:
//   literals and escapes   a  \.  \n \t \r \f \v  \xHH  \x{HHHH}
//   any character          .  (not a line break unless (?s))
//   classes                [abc] [^a-z] [[:alpha:]]  \d \w \s  \D \W \S  (ASCII digits, word characters, spaces)
//   anchors                ^ $ (the text's ends; lines under (?m))  \A \z  \b \B
//   groups                 (x)  (?:x)  (?P<name>x)  (?<name>x)
//   alternation            x|y
//   repetition             x* x+ x? x{n} x{n,} x{n,m}, lazy with a '?' after: x*? x{n,m}?
//   flags                  (?i) (?m) (?s) for the rest of the group, (?i:x) for x alone; i folds ASCII letters
// Text is UTF-8: '.' and classes match whole code points; a byte that is not UTF-8 matches '.' and nothing else.
//     Core::Regex error = Core::Regex("^(?P<file>.+)\\((\\d+),(\\d+)\\): error (\\w+): (.*)$");
//     Core::RegexMatch found;
//     if (error.Search(line, found)) printf("%s:%s\n", found.Group(1).ToString().CStr(), found.Group(2).ToString().CStr());

include "string.ch"
include "stringview.ch"
include "stringbuilder.ch"
include "../Container/array.ch"

namespace Core
{
	// One match: where the whole match and each group start and end in the text (byte offsets).
	struct RegexMatch
	{
		public StringView text;
		public Array<int64> bounds;   // group g starts at bounds[2g] and ends at bounds[2g + 1]; -1 when g took no part

		public int32 GroupCount() const { return (int32)(bounds.Count() / 2) - 1; }   // not counting the whole match
		public bool Matched(int32 group = 0) const { return group >= 0 && group <= GroupCount() && bounds[group * 2] >= 0; }
		public int64 Start(int32 group = 0) const { return Matched(group) ? bounds[group * 2] : (int64)-1; }
		public int64 End(int32 group = 0) const { return Matched(group) ? bounds[group * 2 + 1] : (int64)-1; }
		// The group's text; empty when it took no part.
		public StringView Group(int32 group = 0) const;
	}

	// --- the compiled form (regexcompile.cf, regexvm.cf) --------------------------------------------------------------

	enum class RegexOp { Char, Class, Any, AnyButNewline, Split, Jump, Save, Match, LineStart, LineEnd, TextStart, TextEnd, WordBoundary, NotWordBoundary }

	// Char: `x` is the code point. Class: `x` is the first of its ranges, `y` how many. Split: try `x`, then `y`.
	// Jump: to `x`. Save: the position into bounds slot `x`.
	struct RegexInst
	{
		public RegexOp op;
		public int32 x;
		public int32 y;
	}

	// A range of code points, both ends included.
	struct RegexRange
	{
		public int32 low;
		public int32 high;

		[operator("<")] public bool Less(const RegexRange& other) const { return low < other.low || (low == other.low && high < other.high); }
	}

	class Regex
	{
		[operator("create")] public void Init() {}   // not valid: matches nothing
		[operator("create")] public void Init(StringView pattern) { Compile(pattern); }
		[operator("create")] public void Init(const char* pattern) { Compile(StringView(pattern)); }

		// Whether the pattern compiled; otherwise Error() says why and ErrorOffset() where (a byte offset).
		public bool IsValid() const { return !program.IsEmpty(); }
		public const char* Error() const { return error.CStr(); }
		public int64 ErrorOffset() const { return errorOffset; }
		public int32 GroupCount() const { return groups; }
		// The number of the group `(?P<name>...)` names; -1 when none does.
		public int32 GroupIndex(StringView name) const;

		// Whether the whole of `text` matches.
		public bool Matches(StringView text) const;
		// The same, with the groups in `match`.
		public bool Match(StringView text, RegexMatch& match) const;
		// The leftmost match starting at or after byte `from` (where alternatives and repetition prefer what is written
		// first, and greedy repetition the longest), into `match`.
		public bool Search(StringView text, RegexMatch& match, int64 from = 0) const;
		public bool Contains(StringView text) const;
		// Every match, left to right, not overlapping; after an empty match the next search starts a character on.
		public Array<RegexMatch> FindAll(StringView text) const;
		// `text` with every match replaced by `replacement`, where $0 to $9 and ${n} are a group's text, ${name} a named
		// group's, and $$ a '$'.
		public String Replace(StringView text, StringView replacement) const;

		private void Compile(StringView pattern);
		private bool Run(StringView text, int64 from, bool anchored, Array<int64>& bounds) const;
		private void AppendReplacement(StringBuilder& out, StringView replacement, const RegexMatch& match) const;

		private Array<RegexInst> program;
		private Array<RegexRange> ranges;        // every class's ranges, sorted and merged
		private Array<String> names;             // names[g] names group g; empty when it has none
		private int32 groups;
		private String prefix;                   // bytes every match starts with, found fast in a search
		private String error;
		private int64 errorOffset;
	}
}
