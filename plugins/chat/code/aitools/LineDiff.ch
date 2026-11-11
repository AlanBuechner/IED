// A unified diff of two texts by lines (what an approval card and a write's result show, docs/04): the common head and
// tail set aside, the middle by longest common subsequence when it is small enough, else by the shortest edit script
// (Myers) when it has few enough edits - two edits far apart in a long file - and only else as removed then added.

include "bedrock.ch"

namespace Ai
{
	const int64 c_LineDiffMostCells = 1000000;
	// Lines added and removed past which a middle too big for the cells is shown removed then added: its edit script's
	// trace keeps (edits + 1)^2 entries, as many as the cells.
	const int64 c_LineDiffMostEdits = 999;

	class LineDiff
	{
		public Core::String m_Unified;   // "@@ -a,b +c,d @@" hunks; empty when the texts are the same
		public int64 m_Plus;
		public int64 m_Minus;
	}

	LineDiff DiffLines(Core::StringView before, Core::StringView after, int64 context);
	// The lines of `text`, without their line breaks ("\r\n" or "\n"); a final break starts no line.
	Core::Array<Core::StringView> SplitLines(Core::StringView text);
	// The lines a unified diff adds and removes: its hunks' '+' and '-' lines, never a file's "---" and "+++" lines.
	void CountDiff(Core::StringView unified, int64& plus, int64& minus);
}
