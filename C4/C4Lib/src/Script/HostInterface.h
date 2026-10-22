#pragma once
#include "Core/Core.h"
#include "Macro/MacroTable.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Scripting
{
	/// One source of the host as a script compiles against it (docs/runtime/04 section 3.2).
	struct InterfaceFile
	{
		uint32 unitId = 0;     // its id in the host build; the script compile registers it under the same one
		uint32 project = 0;    // index into HostInterface::projects
		int32 folder = -1;     // the host source folder it is in (0 .. folderCount - 1); -1 for a library file
		std::string path;      // within its folder (or the library), `/`-separated
		bool library = false;  // a file of the default library (Bedrock)
		bool prelude = false;  // the prelude every file sees
		std::string text;      // the interface text (Script/InterfaceText.h)
		std::vector<MacroDefinition> macros; // what an includer receives
	};

	struct InterfaceProject
	{
		std::string name;
		std::vector<uint32> dependencies; // indexes into HostInterface::projects
		std::vector<uint32> folders;      // its source folders
	};

	/// Everything a script compile needs of its host, written beside the host's executable as `<out>.c4i`:
	/// the host's sources as interface text, the names of its symbol table's entries, the types it registered
	/// with reflection, and the bitcode of the functions a script may inline.
	struct HostInterface
	{
		std::array<uint8, 16> buildId{};  // the same id is compiled into the host's symbol table
		uint32 compilerVersion = 0;       // CompilerVersion::Encoded of the compiler that built the host
		std::string targetCpu = "x86-64"; // the processor the host is compiled for (D-62): its scripts are too
		std::string targetFeatures;       // its LLVM feature string (`+avx2,-avx512f,...`)
		bool debugInfo = false;           // the host was built with -g: its scripts are debugged at first (section 6.4)
		std::vector<InterfaceProject> projects;
		uint32 rootProject = 0;
		uint32 folderCount = 0;
		std::vector<InterfaceFile> files;          // in unit-id order
		std::vector<std::string> symbols;          // the symbol table's entries, in its order
		std::vector<std::string> reflectedTypes;   // the qualified names of the types the host registered
		std::vector<std::string> inlineBodies;     // chunks of LLVM bitcode (Script/InlineBodies.h); none for none
	};

	/// The interface as the bytes of a `.c4i` file: magic `C4IF`, a format version, then each field in order.
	std::string SerializeInterface(const HostInterface& host);
	/// Reads what SerializeInterface wrote; false, with the reason, for a file of another format or version, one
	/// that ends early, or one whose target this compiler does not know.
	bool DeserializeInterface(std::string_view bytes, HostInterface& host, std::string* error);

	bool WriteInterface(const HostInterface& host, const std::string& path, std::string* error);
	bool ReadInterface(const std::string& path, HostInterface& host, std::string* error);
} // namespace Scripting
