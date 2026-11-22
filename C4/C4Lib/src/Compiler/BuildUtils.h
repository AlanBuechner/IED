#pragma once

#include "Compiler/CpuTarget.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Macro/PredefinedMacros.h"
#include "Target/TargetAbi.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace C4Compiler
{
	// Common CLI options for the C4 compiler
	struct CompileOptions
	{
		/// One or more input paths (files or folders). All registered folders form the shared include
	/// path, so headers resolve across every input folder (multi-source-folder support).
	Core::Vector<Core::String> inputs;
	Core::String outputFile; // -o; ParseArgs names it output.exe (output for Linux) when -o does not
	std::string targetSystem; // --target <windows-x64|linux-x64>: empty for the host's system (D-86)
	Core::Vector<Core::String> includeDirs;
	Core::String projectsFile; // --projects <file>: the build description (docs/language/15-projects.md)
	bool debugInfo = false; // -g: CodeView in the objects, a .pdb beside the executable
	Subsystem subsystem = Subsystem::Console; // --subsystem
	std::vector<MacroDefine> defines; // -D NAME / -D NAME=VALUE: predefined macros for every file
	bool compileOnly = false;
	bool emitIR = false;
	bool emitBitcode = false; // -bc: write LLVM bitcode (.bc) and stop (the scripting pipeline)
	bool prettyPrint = false;
	bool verbose = false;
	bool exportMacros = false;
	bool gcStats = false; // install the GC runtime's default stats printer in the entry point
	bool timePhases = false; // -ftime: print a per-phase compile timing breakdown (parse/semantic/HIR/codegen)
	int optLevel = 0; // -O<n>: optimization level (0..3). Default 0 = debug, matches C++ compilers.
	unsigned threads = 0;    // -j <n>: threads that lower function bodies and generate code; 0 until ParseArgs picks the default
	unsigned partitions = 0; // --partitions <n>: objects the program is assembled into; 0: by the module's size
	bool incremental = false; // --incremental: each partition optimized alone, so an edit reoptimizes only its own (D-76)
	std::vector<std::string> targetFlags; // -march=<cpu>, -m<feature>, -mno-<feature>, in order (D-62)
	CpuTarget target;                     // what targetFlags select
	bool strictAliasing = true;           // -fno-strict-aliasing turns it off: no alias tags (D-64)
	bool stackMaps = false;               // -fstack-maps: the collector reads C4 frames by stack maps (R16 phase 3)
	bool runTests = false; // -test: emit a synthetic main that runs [test] functions instead of user main
	Core::String testJsonPath; // -test-json <path>: write a machine-readable summary next to the console output
	Core::String reflectTag;   // -reflect-tag <tag>: source tag for reflection registration (scripting hot reload)
	bool scriptHost = false;   // --script-host: a symbol table scripts link against, and <out>.c4i (docs/runtime/04)
	bool warningsAsErrors = false; // -Werror: every warning fails the build
	bool noWarnings = false;       // -w: warnings are not reported
	Core::String intermediateDir;  // --int-dir: where the intermediates (.obj, .ll, .bc) go; empty: next to the output
	Core::Vector<Core::String> linkArguments; // -l<lib> / -L<dir>, passed to the link as written
	bool printVersion = false;     // --version
	bool printHelp = false;        // -h
	Core::String error;            // what the command line got wrong; empty when it parsed
	};

	// Parse command-line arguments into CompileOptions
	CompileOptions ParseArgs(int argc, char** argv);

	// Where a build's intermediates go: --int-dir, else the output's folder (empty: the current folder).
	fs::path IntermediateFolder(const CompileOptions& options);

	// Print usage/help message
	void PrintUsage();

	// The toolchain's clang.exe (Toolchain::Clang), or empty after printing where it was looked for.
	std::string ClangPath();

	/// What clang's driver would hand lld-link for one argument of a link's libraries: `-l<name>` is `<name>.lib`,
	/// `-L<dir>` a search folder, `-Wl,a,b` its arguments; an object, a library or a resource file goes as it is.
	std::vector<std::string> LinkerArguments(const std::string& argument);

	/// The same for clang's driver linking for Linux: `-l<name>` searches for lib<name>, but `-l` of a library's file (a
	/// path, as Nuke gives a C++ dependency's `.a`) is that file, which ld would look up as a name and not find.
	std::string LinuxLinkerArgument(const std::string& argument);

	// Compile a foreign C/C++ source file to an object file for `system` using the bundled clang, with its target's flags
	// (TargetAbi::ForeignSourceFlags: for Linux against the Linux sysroot, position independent).
	// language is "c" or "c++"; defines/includeDirs are passed as -D / -I flags, targetFlags (D-62) and compileFlags (the
	// build's -O and -g, BUG-285) as written. clang also writes the headers it read to `<objPath>.d`.
	bool CompileForeignSource(const Core::String& srcPath, const Core::String& objPath,
		const Core::String& language, const Core::Vector<Core::String>& defines,
		const Core::Vector<Core::String>& includeDirs, TargetSystem system, const std::vector<std::string>& targetFlags,
		const std::vector<std::string>& compileFlags, bool verbose);

	// Run an external tool directly (no shell), inheriting stdout/stderr so its output streams live.
	bool RunTool(const std::vector<std::string>& args);


	// Remove a file if it exists (no-op on failure)
	void RemoveIfExists(const std::string& path);

	// Run an external tool, capturing stdout/stderr. Returns the exit code (-1 on spawn failure). RunTool and this are
	// the system's: Compiler/ToolProcessWindows.cpp and Compiler/ToolProcessPosix.cpp.
	bool RunToolWithOutput(const Core::Vector<Core::String>& args, int& exitCode,
		std::string& outStdout, std::string& outStderr);
}
