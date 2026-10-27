// The AI tool calls plugins contribute (`toolCalls`, docs/26 section 8) that run a command: the plugin's argv with
// the call's arguments put in - "${args.<name>}"; an argument that is a list, given as a whole word, is a word per item
// - and settings ("${config:<key>}", as in task steps), run in the workspace as shell_run runs, with "shell(<command>)"
// as its subject for the permission rules, and never in an untrusted workspace. Its output is the exit code and the
// output's tail, or the whole output for `output: json`.

include "Tool.ch"

namespace Ai
{
	// The tool `entry` (a `toolCalls` item) describes; null with `problem` when it runs no command.
	ToolSpec* CommandTool(const Serialization::DomNode& entry, Core::StringView pluginId, Core::String& problem);
	// A setting's effective value as text, for ${config:<key>}; a language tool's program, for ${tool:<id>}.
	alias ToolSetting = func<Core::String(Core::StringView)>;
	// `words` with ${args.<name>} replaced from `args`, ${config:<key>} from `setting` and ${tool:<id>} from `tool` (what is
	// missing is empty).
	Core::Array<Core::String> ExpandToolArgv(const Core::Array<Core::String>& words, const Serialization::DomNode& args, const ToolSetting& setting, const ToolSetting& tool);
	// Whether `program` is a batch file (.cmd, .bat): it runs only through cmd.exe (a setting naming nuke.cmd).
	bool IsBatchFile(Core::StringView program);
	// `argv` as the line cmd.exe runs, each word quoted; false with `problem` when a word holds what cmd.exe reads even
	// inside quotes (a quote, '%', a line break), so no argument can become a command of its own.
	bool BatchLine(const Core::Array<Core::String>& argv, Core::String& line, Core::String& problem);
}
