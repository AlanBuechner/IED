// The texts the runtime sends that are not the conversation (docs/14): the base prompt, the style rules and each
// mode's instructions, assembled with the workspace's AGENTS.md, its plugins' sections, a summary and its skills into
// the system message in a fixed order (so the prefix stays byte-identical across steps); and the harness notes of
// docs/12, by name, with {placeholders}. They are read from src/ai/prompts in the install.

include "chat.ch"

namespace Ai
{
	const int64 kAgentsMdMostBytes = 10000;   // about 2,500 tokens (docs/14 item 4)

	// What the system message holds of the workspace, each part empty when there is none.
	class SystemContext
	{
		public Core::String agentsMd;    // its AGENTS.md (docs/14 item 4)
		public Core::String sections;    // its plugins' sections (item 5)
		public Core::String workspace;   // the summary (item 6)
		public Core::String skills;      // the list of its skills (item 8)
		public Core::String docs;        // its plugins' reference docs, one line each
		public Core::String deferred;    // the tools tool_search loads, one line each (item 7, docs/13)
		public Core::String agents;      // the sub-agents it may run and when delegating pays (docs/04); "" for none
	}

	class Prompts
	{
		[operator("create")] public void Init() {}

		// Reads the folder; what is missing is named in `problems` and left empty.
		public void Load(const FS::Path& folder, Core::Array<Core::String>& problems);
		// The system message for a chat in `mode` in the workspace `context` describes.
		public Core::String System(ChatMode mode, const SystemContext& context) const;
		// The harness note `name` with each {key} replaced by its value in `values` ("key", "value", "key", ...).
		public Core::String Note(const char* name, const Core::Array<Core::String>& values) const;
		public Core::String Note(const char* name) const;

		private Core::String base;
		private Core::String style;
		private Core::HashMap<Core::String, Core::String> modes;   // "ask" -> its instructions
		private Core::HashMap<Core::String, Core::String> notes;   // "continue_after_max_tokens" -> its text
	}

	// The "## name" sections of a Markdown file: name -> the text under it, trimmed.
	Core::HashMap<Core::String, Core::String> MarkdownSections(Core::StringView text);
}
