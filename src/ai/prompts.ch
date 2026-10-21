// The texts the runtime sends that are not the conversation (docs/14): the base prompt, the style rules and each
// mode's instructions, assembled with the workspace's AGENTS.md and a summary into the system message in a fixed order
// (so the prefix stays byte-identical across steps); and the harness notes of docs/12, by name, with {placeholders}.
// They are read from src/ai/prompts in the install.

include "chat.ch"

namespace Ied
{
	const int64 kAgentsMdMostBytes = 10000;   // about 2,500 tokens (docs/14 item 4)

	class Prompts
	{
		[operator("create")] public void Init() {}

		// Reads the folder; what is missing is named in `problems` and left empty.
		public void Load(const FS::Path& folder, Core::Array<Core::String>& problems);
		// The system message for a chat in `mode`, with the workspace's AGENTS.md (empty: none) and summary.
		public Core::String System(ChatMode mode, Core::StringView agentsMd, Core::StringView workspace) const;
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
