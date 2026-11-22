// How the server tells which file or folder a path names (docs/10 "Workspaces"): Windows' names ignore case, Linux's do
// not, so two folders whose names differ only in case are one folder on Windows and two on Linux - two workspaces, each
// trusted on its own. Searching (globs, Find in Files, Go to All) ignores case on every system; this is only identity.

include "bedrock.ch"

namespace Ied
{
	// Whether this system's file names ignore ASCII case: Windows' do, Linux's do not.
	bool PathsIgnoreCase();
	// A byte of a path as paths compare: '\' as '/', and A-Z as a-z where names ignore case.
	int32 PathByte(int32 c);
}
