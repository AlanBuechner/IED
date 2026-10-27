// What the nuke plugin's package fetch (PackageFetcher.ch) decides from text alone: whether a workspace uses Nuke
// packages, how often it fetches, the command that fetches and what a failed fetch says.

include "api/Process.ch"

namespace NukePlugin
{
	// Whether a nuke.toml uses packages: a [sources] or [packages] table, or a table under one of them.
	bool UsesPackages(Core::StringView manifest);
	// The time between two fetches for a nuke.packages.fetchMinutes value (JSON): its minutes in ms; 0 when it is off.
	int64 FetchIntervalMs(Core::StringView settingJson);
	// The nuke.command setting's value (JSON) as a command: its string without the quotes it may be written with,
	// "nuke" when it is unset or empty.
	Core::String NukeCommand(Core::StringView settingJson);
	// `<program> package status --fetch`, a batch file run through cmd.exe.
	Core::Array<Core::String> FetchCommand(Core::StringView program);
	// What went wrong in a fetch's run: why it did not run, else Nuke's error, else the end of its output; "" when it
	// worked.
	Core::String FetchFailure(const IED::CommandResult& ran);

	const char* c_FetchMinutesKey = "nuke.packages.fetchMinutes";
	const char* c_CommandKey = "nuke.command";
	const int64 c_FetchSeconds = 180;
	const int64 c_FailureBytes = 2000;
}
