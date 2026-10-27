// A step whose program is not there (docs/06 "Steps"): the program and the setting a step names it by
// (`${config:KEY}` first, as the Nuke preset names Nuke), whether cmd would find it, and the line that says how to fix it.

include "bedrock.ch"

namespace Ied
{
	// KEY when `text` starts with ${config:KEY}, quoted or not; "" otherwise.
	Core::String ProgramSetting(Core::StringView text);

	// The first word of a command line, its quotes taken off.
	Core::String ProgramWord(Core::StringView commandLine);

	// Whether cmd would find `program`: a file as given, or a name in `cwd` or a folder of `path` (`;`-separated), as it
	// is or with one of `extensions` (PATHEXT).
	bool FindsProgram(Core::StringView program, Core::StringView cwd, Core::StringView path, Core::StringView extensions);
	// The file cmd would run for `program`, found as FindsProgram finds it; empty when there is none.
	Core::String FoundProgram(Core::StringView program, Core::StringView cwd, Core::StringView path, Core::StringView extensions);
	// FoundProgram with this process's PATH and PATHEXT, not looking in a current folder: the program a name or a path
	// names; empty when there is none.
	Core::String FindProgramFile(Core::StringView program);

	// What says `program` was not found and how to give it; the note is that as a red line of a run's output.
	Core::String MissingProgramText(Core::StringView program, Core::StringView setting);
	Core::String MissingProgramNote(Core::StringView program, Core::StringView setting);
}
