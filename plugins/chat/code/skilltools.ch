// The skill tools (docs/04 "Skills", docs/13): skill_search finds the workspace's skills - its own, its plugins' and
// the app's - by words, and skill_read gives one's SKILL.md body or another file of its folder, with where the skill
// lives (a plugin's as plugins:/<id>/..., read-only) and what lies beside it. Both read only.

include "tool.ch"

namespace Ai
{
	const int64 kSkillSearchMost = 10;
	const int64 kSkillReadMostBytes = 64 * 1024;
	const int64 kSkillPlaceMostEntries = 30;   // of a skill's folder, named after its SKILL.md

	void AddSkillTools(ToolRegistry& registry);
}
