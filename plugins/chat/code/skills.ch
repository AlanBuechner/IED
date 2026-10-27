// Skills (docs/04 "Skills", shared/host-api/README.md "Skill file format"): folders <name>/SKILL.md whose YAML
// frontmatter gives a name, a description and the keywords (or triggers) they are found by. A workspace's skills are
// its own (.ied/skills/), its plugins' `skills` folders and the app's skills folder, the first of a name winning:
// project > plugin > app. The system message lists them (docs/14 item 8); skill_search and skill_read reach the rest.

include "bedrock.ch"

namespace Ai
{
	class Skill
	{
		public Core::String name;
		public Core::String description;
		public Core::Array<Core::String> keywords;   // `keywords` and `triggers`
		public FS::Path file;                        // its SKILL.md
		public Core::String source;                  // "project", "plugin ied.nuke", "app"
		public Core::String shown;                   // its SKILL.md as the file tools name it ("plugins:/ied.nuke/skills/nuke/SKILL.md"); "" for none
		public bool readOnly;                        // a plugin's: its files change only in developer mode (docs/13 "Plugin folders")
	}

	// A folder of skill folders and whose they are.
	class SkillFolder
	{
		public FS::Path folder;
		public Core::String source;
		public Core::String shown;     // the folder as the file tools name it (".ied/skills", "plugins:/c4/skills"); "" for none
		public bool readOnly;
	}

	// One SKILL.md; null with `problem` when its frontmatter does not read or lacks a name or a description.
	Skill* ReadSkill(const FS::Path& file, Core::StringView source, Core::String& problem);
	// The skills of `folders` in order, the first of a name winning; a skill that does not read is a problem.
	Core::Array<Skill*> ReadSkills(const Core::Array<SkillFolder*>& folders, Core::Array<Core::String>& problems);
	// The skills `query` names best (its words in their name, description and keywords; a name counts most), best
	// first, at most `most`; none when no word is found.
	Core::Array<Skill*> SearchSkills(const Core::Array<Skill*>& skills, Core::StringView query, int64 most);
	// How the system message lists skills (docs/14 item 8): every one up to `listed`, past that the `shown` the chat's
	// first message names best (the first others when it names fewer); at most `bytes` of lines in all.
	class SkillsIndexLimits
	{
		public int64 listed;
		public int64 shown;
		public int64 bytes;
	}

	// The system message's list: "- name: description" lines within `limits`, and a line saying how many more
	// skill_search finds when some are left out.
	Core::String SkillsIndex(const Core::Array<Skill*>& skills, Core::StringView hint, const SkillsIndexLimits& limits);
	SkillsIndexLimits DefaultSkillsIndexLimits();
	// A skill's SKILL.md without its frontmatter.
	Core::String SkillBody(const Skill* skill);
	Skill* SkillNamed(const Core::Array<Skill*>& skills, Core::StringView name);

	const int64 kSkillIndexDescriptionBytes = 200;
}
