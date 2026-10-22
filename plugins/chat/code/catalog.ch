// The core toolset every request carries (docs/13 "Core toolset"): the file, code, work and skill tools, and the run's
// own - tasks_set, task_update, ask_user and finish - which the run carries out itself (docs/12); and the deferred groups
// that need no chat (media, code).

include "files.ch"
include "codeinfo.ch"
include "work.ch"
include "skilltools.ch"
include "media.ch"

namespace Ai
{
	ToolRegistry* CoreTools();
}
