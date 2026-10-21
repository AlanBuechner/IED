// Finding files by name (Go To All, fs.files): the query's characters in order within a path, any case, scored so that
// a match inside the file's own name, runs of consecutive characters and the starts of words come first, then shorter
// paths. Honest.

include "walk.ch"

namespace Ied
{
	struct FileMatch
	{
		public Core::String path;
		public int64 score;
	}

	// The score of `path` for `query` (higher is better), or -1 when the query's characters are not all in it in order.
	int64 FuzzyScore(Core::StringView query, Core::StringView path);
	// The best `most` files `walk` visits for `query`, best first, ties in path order.
	Core::Array<FileMatch> FindFiles(FileWalk& walk, Core::StringView query, int64 most);
}
