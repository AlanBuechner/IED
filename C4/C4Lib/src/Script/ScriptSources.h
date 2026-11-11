#pragma once
#include "Core/Core.h"
#include "Macro/MacroTable.h"
#include "Script/HostInterface.h"
#include "Script/ScriptLayer.h"
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
		uint32 interfaceUnits = 0;            // units 1..interfaceUnits are the host's and its layers', under the ids they had
		std::vector<uint32> scriptUnits;      // the script's files, in the order given
		uint32 scriptProject = 0;
		std::unordered_map<uint32, MacroTable> interfaceMacros; // what each interface unit exports, for the macro pass
		std::unordered_map<uint32, MacroTable> exportedMacros;  // what each of the script's files exports, once the macro pass has run
		std::vector<std::string> scriptFolders; // the folders its includes resolve from
	};

	/// The folder no disk path has that the interface's files are placed under (`//c4i/f<n>/...`, `//c4i/lib/...`).
	inline constexpr const char* kInterfaceRoot = "//c4i";

	/// What to register for a script (ScriptCompileOptions' fields).
	struct ScriptSourcesDesc
	{
		const std::string& name;
		const std::vector<ScriptFile>& files;
		const std::map<std::string, std::string>& defines;
		const std::string& project;
		const std::vector<std::string>& roots;
		const ScriptChain* base;
	};

	/// Registers `host`'s files, those of the layers `desc` extends and then `desc`'s files into `compiler`: the host's
	/// projects, with each folder and the library in a place of its own under kInterfaceRoot, a project for each layer on
	/// the one below, and the script as one more project that uses the host's root or the top layer (so includes resolve
	/// among the host's files as they did in the host, and the script reaches every host header). A file given as text
	/// sits in the script's own folder under kInterfaceRoot. Empty, with the reason, when a script file cannot be read or
	/// the interface's ids do not come out as recorded.
	std::optional<ScriptUnits> RegisterScriptSources(CompilerInstance& compiler, const HostInterface& host, const ScriptSourcesDesc& desc, std::string* error);
} // namespace Scripting
