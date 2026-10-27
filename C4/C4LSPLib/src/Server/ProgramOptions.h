#pragma once
#include "Core/Core.h"
#include "Json/JsonValue.h"
#include "Compiler/CpuTarget.h"
#include <map>
#include <string>
#include <vector>

namespace C4LSP::Server
{
	// How one program builds, as the options file Nuke writes for it says (docs/tools/03 "Configuration"): what the server
	// compiles to analyze the documents of that program.
	struct ProgramOptions
	{
		std::string name;                           // the program's name in initializationOptions.programs; empty for the options themselves
		std::vector<std::string> includePaths;      // -I folders
		std::map<std::string, std::string> defines; // predefined macros
		std::vector<std::string> targetFlags;       // the build's -march / -m flags (D-62)
		std::string libraryFolder;                  // <c4Home>/Bedrock; empty: the server's own
		std::string projectsFile;                   // the build description (docs/language/15-projects.md)

		// The processor the flags select; the default target when they select none.
		Scripting::CpuTarget Target() const;
		bool operator==(const ProgramOptions& other) const;
		bool operator!=(const ProgramOptions& other) const { return !(*this == other); }
	};

	// One program's options from a JSON object - initializationOptions, or an entry of its `programs`. A malformed entry is
	// left out: a bad client option never breaks the server.
	ProgramOptions ReadProgramOptions(const std::string& name, const Json::Value& options);

	// initializationOptions.programs: an object of options by program name, in name order, or an array of them (named by
	// their index). Anything else is no program.
	std::vector<ProgramOptions> ReadPrograms(const Json::Value& programs);
} // namespace C4LSP::Server
