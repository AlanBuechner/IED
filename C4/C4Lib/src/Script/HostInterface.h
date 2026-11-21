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
		// A unit of CBind's bindings (R8): a declaration another binding unit repeats is one declaration in the script
		// compile, as in the host's (ModelBuilder::RepeatsBinding).
		bool binding = false;
		std::vector<uint32> bindings; // the binding units it includes, by unit id
		std::vector<MacroDefinition> macros; // what an includer receives
		// The templates it declares (a class with a member template counts), and those it adds to - by a specialization, a
		// member defined out of line, an extension - as Script/TemplateNames.h names them (qualified; an addition with the
		// namespace it is written in): a template's additions apply wherever it is used, so a script compile reading the
		// template reads the files that add to it (section 4, step 3).
		std::vector<std::string> templates;
		std::vector<std::string> extendedTemplates;
	};

	/// A function symbol two functions or more had (SymbolTable::CollidedSymbols): the later ones got `_<n>` in the order
	/// their records came in, so which one is which follows from those units.
	struct CollidedSymbol
	{
		std::string symbol;
		std::vector<uint32> units; // the units that declared them, in that order
	};

	struct InterfaceProject
	{
		std::string name;
		std::vector<uint32> dependencies; // indexes into HostInterface::projects
		std::vector<uint32> folders;      // its source folders
		bool reflect = true;              // its `reflect` setting (D-89): its scripts' instances of its templates follow it
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
		// Its function symbols two functions had: a script compile reading one of their units reads all of them
		// (section 4, step 3), so their suffixes come out as in the host.
		std::vector<CollidedSymbol> collidedSymbols;
	};

	/// The interface as the bytes of a `.c4i` file: magic `C4IF`, a format version, then each field in order.
	std::string SerializeInterface(const HostInterface& host);
	/// Reads what SerializeInterface wrote; false, with the reason, for a file of another format or version, one
	/// that ends early, or one whose target this compiler does not know.
	bool DeserializeInterface(std::string_view bytes, HostInterface& host, std::string* error);

	/// SerializeInterface's bytes with the build id they get: XXH3's 128-bit hash of them with the id zero, written
	/// into the bytes and `host`. One serialization of an interface whose inline bodies are tens of megabytes.
	std::string SealInterface(HostInterface& host);

	bool WriteInterface(const HostInterface& host, const std::string& path, std::string* error);
	/// Bytes SerializeInterface or SealInterface made.
	bool WriteInterfaceBytes(const std::string& bytes, const std::string& path, std::string* error);
	/// Bytes SealInterface made, unless the file at `path` holds them already (the same size and build id, which is a
	/// hash of all of them): a build that changed nothing a script sees leaves the interface as it was.
	bool WriteSealedInterface(const std::string& bytes, const std::string& path, std::string* error);
	bool ReadInterface(const std::string& path, HostInterface& host, std::string* error);
} // namespace Scripting
