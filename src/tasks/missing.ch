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

	// The red line that says `program` was not found and how to give it.
	Core::String MissingProgramNote(Core::StringView program, Core::StringView setting);
}
