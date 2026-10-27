// The commands language tools run (docs/26 "Language tools"): a version asked, an install's step. A program that is a
// batch file (a .cmd or .bat) runs through cmd.exe with each word quoted, as a plugin's command tool does; a word cmd.exe
// would read even inside quotes is refused.

include "bedrock.ch"

namespace Ied
{
	// Whether `program` is a batch file (by its extension, any case).
	bool IsBatchProgram(Core::StringView program);
	// `argv` as one line for cmd.exe, every word quoted; false with `problem` when a word holds a quote, '%' or a line
	// break.
	bool QuotedBatchLine(const Core::Array<Core::String>& argv, Core::String& line, Core::String& problem);
	// What starts `argv` in `cwd` with `environment` ("NAME=value" over this process's); false with `problem` when a
	// batch file's words cannot be passed.
	bool CommandStartInfo(const Core::Array<Core::String>& argv, Core::StringView cwd, const Core::Array<Core::String>& environment, Process::StartInfo& info, Core::String& problem);
	// The first line of `output` that is not blank, at most 300 bytes.
	Core::String FirstLine(Core::StringView output);
	// `environment` with `prepend` (folders, ';'-separated) before this process's PATH, or before the PATH an entry of
	// `environment` already gives; as it is when `prepend` is empty.
	Core::Array<Core::String> WithPathFirst(const Core::Array<Core::String>& environment, Core::StringView prepend);
}
