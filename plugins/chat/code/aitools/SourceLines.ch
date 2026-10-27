// Source lines under errors: a build's or the Error List's `path:line:col: error: message` lines, each followed by the
// line of source it names, so a model fixes the error without reading the file first.

include "bedrock.ch"

namespace Ai
{
	const int64 c_SourceLinesMost = 20;
	const int64 c_SourceLineMostBytes = 200;

	// `problems` with, under each of its first kSourceLinesMost error lines whose file `read` gives (by the path the
	// line names), that file's line.
	Core::String WithSourceLines(Core::StringView problems, func<bool(Core::StringView path, Core::String& text)> read);

	// Line `number` (from 1) of `text`, without its line break; empty past the end.
	Core::StringView SourceLineAt(Core::StringView text, int64 number);
}
