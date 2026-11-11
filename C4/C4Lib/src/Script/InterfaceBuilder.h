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
	class SourceManager;

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
	/// macro pass), plus `build`. The build id is a hash of all of it (SealInterface), and `serialized`, when given,
	/// gets the interface's bytes. Empty, with the reason, when a file's declarations do not fit its text.
	std::optional<HostInterface> BuildInterface(CompilerInstance& compiler, const std::unordered_map<std::string, MacroTable>& exportedMacros,
		HostBuild build, std::string* error, std::string* serialized = nullptr);

	/// The script files `units` as a script over them compiles against them (docs/runtime/04 section 5.2): the interface
	/// text of each and the macros it exports, with its folder an index into `folders`. Empty, with the reason, when a
	/// file's declarations do not fit its text.
	std::optional<std::vector<InterfaceFile>> LayerFiles(CompilerInstance& compiler, const std::vector<uint32>& units,
		const std::unordered_map<uint32, MacroTable>& exportedMacros, const std::vector<std::string>& folders, std::string* error);

	/// The C functions the prelude declares (their link symbols): a host lists them whether or not it calls them.
	std::vector<std::string> PreludeFunctionSymbols(const ProgramModel& model);
	/// The runtime functions the default library (Bedrock) declares by `[link("c4_...")]`: a host lists them whether or not it
	/// calls them, so a script reaches every Bedrock API its host has the headers of (the collector's hooks a VM in a script uses,
	/// GC::RegisterMarkEnd, while the host calls none).
	std::vector<std::string> LibraryRuntimeSymbols(const ProgramModel& model, const SourceManager& sources);
} // namespace Scripting
