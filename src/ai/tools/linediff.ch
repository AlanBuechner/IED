// A unified diff of two texts by lines (what an approval card and a write's result show, docs/04): the common head and
// tail set aside, the middle by longest common subsequence when it is small enough, else as removed then added.

include "bedrock.ch"

namespace Ied
{
	const int64 kLineDiffMostCells = 1000000;

	class LineDiff
	{
		public Core::String unified;   // "@@ -a,b +c,d @@" hunks; empty when the texts are the same
		public int64 plus;
		public int64 minus;
	}

	LineDiff DiffLines(Core::StringView before, Core::StringView after, int64 context);
	// The lines of `text`, without their line breaks ("\r\n" or "\n"); a final break starts no line.
	Core::Array<Core::StringView> SplitLines(Core::StringView text);
}
