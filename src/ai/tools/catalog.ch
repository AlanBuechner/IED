// The core toolset every request carries (docs/13 "Core toolset"): the file and work tools, and the run's own -
// tasks_set, task_update, ask_user and finish - which the run carries out itself (docs/12).

include "files.ch"
include "work.ch"

namespace Ied
{
	ToolRegistry* CoreTools();
}
