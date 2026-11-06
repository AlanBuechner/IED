// A board's history kept across restarts (docs/32 "Changes, undo and the server's ownership"): written beside the
// board's per-machine state each time the board is saved, with the digest of the board text it goes with, and read
// back when the board opens again and its file is still that text - so undo, redo and History's Revert work after a
// restart, and a file changed while the server was not running starts its history empty.

include "Board.ch"

namespace Boards
{
	// The history of `board` as a file's text, for the board saved as `boardText`.
	Core::String WriteHistory(const Board& board, Core::StringView boardText);
	// The history `text` holds, into `board` (read from `boardText`), when it was written for that text; false, the
	// board's history untouched, when it was written for another text or does not read.
	bool ReadHistory(Core::StringView text, Core::StringView boardText, Board& board);
}
