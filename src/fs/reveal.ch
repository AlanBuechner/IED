// Showing a path in the system's file manager (fs.reveal, docs/10 "Files"): a folder opens in it, a file opens its
// folder with the file selected. The command never hands the file manager a file to open, so nothing can be started
// this way: Windows Explorer gets a file only after `/select,`.

include "bedrock.ch"

namespace Ied
{
	// The file manager's command that shows `target` (absolute; a folder when `folder`): Explorer with the path in
	// backslashes, which `/select,` needs.
	Process::StartInfo RevealCommand(const FS::Path& target, bool folder);
}
