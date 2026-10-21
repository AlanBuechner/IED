// The skill tools (docs/04 "Skills", docs/13): skill_search finds the workspace's skills - its own, its plugins' and
// the app's - by words, and skill_read gives one's SKILL.md body or another file of its folder. Both read only.

include "tool.ch"

namespace Ied
{
	const int64 kSkillSearchMost = 10;
	const int64 kSkillReadMostBytes = 64 * 1024;

	void AddSkillTools(ToolRegistry& registry);
}
