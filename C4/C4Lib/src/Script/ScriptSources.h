#pragma once
#include "Core/Core.h"
#include "Macro/MacroTable.h"
#include "Script/HostInterface.h"
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class CompilerInstance;

	/// One file of a script: on disk at `path`, or given as `text` under the name `path`.
	struct ScriptFile
	{
		std::string path;
		std::optional<std::string> text;
	};

	/// Where a script compile put its sources (docs/runtime/04 section 4, step 1).
	struct ScriptUnits
	{
		uint32 interfaceUnits = 0;            // units 1..interfaceUnits are the host's, under their host ids
		std::vector<uint32> scriptUnits;      // the script's files, in the order given
		uint32 scriptProject = 0;
		std::unordered_map<uint32, MacroTable> interfaceMacros; // what each interface unit exports, for the macro pass
	};

	/// The folder no disk path has that the interface's files are placed under (`//c4i/f<n>/...`, `//c4i/lib/...`).
	inline constexpr const char* kInterfaceRoot = "//c4i";

	/// Registers `host`'s files and then `files` into `compiler`: the host's projects, with each folder and the
	/// library in a place of its own under kInterfaceRoot, and the script as one more project that uses the host's
	/// root (so includes resolve among the host's files as they did in the host, and the script reaches every host
	/// header). A file given as text sits in the script's own folder under kInterfaceRoot. Empty, with the reason,
	/// when a script file cannot be read or the interface's ids do not come out as recorded.
	std::optional<ScriptUnits> RegisterScriptSources(CompilerInstance& compiler, const HostInterface& host, const std::string& name,
		const std::vector<ScriptFile>& files, const std::map<std::string, std::string>& defines, std::string* error);
} // namespace Scripting
