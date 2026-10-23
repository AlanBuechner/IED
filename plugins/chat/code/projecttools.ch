// The project tools (docs/13 `projects`): the open solution's projects, New Project's templates, a project made from
// one, an existing folder made an IED project (this workspace becoming a solution in place, a folder of it joining the
// solution, or a folder elsewhere opening as a solution of its own), and a project moved or removed - through the
// server's project.* operations, so the Solution Explorer of every window shows each change at once.

include "tool.ch"

namespace Ai
{
	void AddProjectTools(ToolRegistry& registry);
}
