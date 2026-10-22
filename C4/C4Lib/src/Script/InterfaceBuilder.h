#pragma once
#include "Core/Core.h"
#include "Macro/MacroTable.h"
#include "Script/HostInterface.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scripting
{
	class CompilerInstance;
	class ProgramModel;

	/// What the backend contributes to a host's interface.
	struct HostBuild
	{
		std::vector<std::string> symbols;        // the symbol table's entries (Script/HostTable.h)
		std::vector<std::string> inlineBodies;   // Script/InlineBodies.h
		std::vector<std::string> reflectedTypes; // the types the program's reflection registers
		bool debugInfo = false;                  // -g: the host debugs its scripts at first
	};

	/// The interface of the program `compiler` compiled (docs/runtime/04 section 3.2): its projects and folders, and
	/// every source with its interface text and the macros it exports (`exportedMacros`, by MacroIdentity, from the
	/// macro pass), plus `build`. The build id is a hash of all of it. Empty, with the reason, when a file's
	/// declarations do not fit its text.
	std::optional<HostInterface> BuildInterface(CompilerInstance& compiler, const std::unordered_map<std::string, MacroTable>& exportedMacros,
		HostBuild build, std::string* error);

	/// The C functions the prelude declares (their link symbols): a host lists them whether or not it calls them.
	std::vector<std::string> PreludeFunctionSymbols(const ProgramModel& model);
} // namespace Scripting
