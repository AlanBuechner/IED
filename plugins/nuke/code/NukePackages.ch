// What the nuke plugin's package code (PackageFetcher.ch, PackageManager.ch) decides from text alone: whether a
// workspace uses Nuke packages, how often it fetches, the commands it runs and what they said.

include "api/Process.ch"

namespace NukePlugin
{
	// Whether a nuke.toml uses packages: a [dependencies] table, or a table under it.
	bool UsesPackages(Core::StringView manifest);
	// The time between two fetches for a nuke.packages.fetchMinutes value (JSON): its minutes in ms; 0 when it is off.
	int64 FetchIntervalMs(Core::StringView settingJson);
	// The nuke.command setting's value (JSON) as a command: its string without the quotes it may be written with,
	// "nuke" when it is unset or empty.
	Core::String NukeCommand(Core::StringView settingJson);
	// `<program> package <words...>`, a batch file run through cmd.exe.
	Core::Array<Core::String> PackageCommand(Core::StringView program, const Core::Array<Core::String>& words);
	// The JSON document Nuke printed among its notes: from the first line that starts with '{' to the last that starts
	// with '}' (its --json output is indented, so only its own braces start a line); "" when there is none.
	Core::String PrintedJson(Core::StringView output);
	// Whether the Package Manager may run `nuke package <action>`: update, add, remove, develop, lock, pull or release.
	bool IsPackageAction(Core::StringView action);
	// What went wrong in a fetch's run: why it did not run, else Nuke's error, else the end of its output; "" when it
	// worked.
	Core::String FetchFailure(const IED::CommandResult& ran);

	const char* c_FetchMinutesKey = "nuke.packages.fetchMinutes";
	const char* c_CommandKey = "nuke.command";
	const int64 c_FetchSeconds = 180;
	const int64 c_FailureBytes = 2000;
}
