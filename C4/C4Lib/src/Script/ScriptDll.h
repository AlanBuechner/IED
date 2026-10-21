#pragma once
#include "Script/ScriptJit.h"
#include <string>

namespace Scripting
{
	/// Where a debug script's module goes: `<folder>/<name>.dll`, its `.pdb` beside it. Each load is a name of its own,
	/// so a debugger never sees a file change under it.
	struct ScriptDllFile
	{
		std::string folder;
		std::string name;
	};

	/// A script's tag as a module name: the characters a file name cannot hold become '-' (`ied.git#3` stays as it is).
	std::string ScriptDllName(const std::string& tag, uint64 load);

	/// The folder this process's debug script modules go in: `<root>/<process id>`. The folders of processes that have
	/// ended go first, with what a crash left in them.
	std::string ScriptDllFolder(const std::string& root);

	/// The folder `ScriptDllFolder` made, removed once nothing is left in it.
	void RemoveScriptDllFolder(const std::string& folder);

	/// Links `code` and its debug information into a DLL and its PDB and loads it (docs/runtime/04 section 6.4): an
	/// ordinary module, which debuggers, DbgHelp and crash dumps see like any other. Every name the script takes from
	/// outside goes through an import slot, filled from `host` (and the compiler's support functions, and the process's
	/// DLLs) before any of its code runs; every name it defines is exported. Empty, with the reason, when it does not
	/// link or load.
	std::unique_ptr<LinkedScript> LinkScriptDll(ScriptCode code, const HostSymbolTable& host, const CpuTarget& target,
		const ScriptDllFile& file, std::string* error);
} // namespace Scripting
