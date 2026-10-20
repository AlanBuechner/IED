#pragma once

#include "Compiler/CpuTarget.h"
#include "Core/String.h"
#include "Core/Vector.h"
#include "Macro/PredefinedMacros.h"
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Scripting
{
	// Common CLI options for the C4 compiler
	struct CompileOptions
	{
		/// One or more input paths (files or folders). All registered folders form the shared include
	/// path, so headers resolve across every input folder (multi-source-folder support).
	Core::Vector<Core::String> inputs;
	Core::String outputFile = "output.exe";
	Core::Vector<Core::String> includeDirs;
	Core::String projectsFile; // --projects <file>: the build description (docs/language/15-projects.md)
	bool debugInfo = false; // -g: CodeView in the objects, a .pdb beside the executable
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
	unsigned threads = 0;    // -j <n>: threads that generate code; 0 until ParseArgs picks the default
	unsigned partitions = 0; // --partitions <n>: objects the program is assembled into; 0: by the module's size
	bool incremental = false; // --incremental: each partition optimized alone, so an edit reoptimizes only its own (D-76)
	std::vector<std::string> targetFlags; // -march=<cpu>, -m<feature>, -mno-<feature>, in order (D-62)
	CpuTarget target;                     // what targetFlags select
	bool strictAliasing = true;           // -fno-strict-aliasing turns it off: no alias tags (D-64)
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

	// Compile a foreign C/C++ source file to an object file using the bundled clang.
	// language is "c" or "c++"; defines/includeDirs are passed as -D / -I flags, targetFlags (D-62) and compileFlags (the
	// build's -O and -g, BUG-285) as written. clang also writes the headers it read to `<objPath>.d`.
	bool CompileForeignSource(const Core::String& srcPath, const Core::String& objPath,
		const Core::String& language, const Core::Vector<Core::String>& defines,
		const Core::Vector<Core::String>& includeDirs, const std::vector<std::string>& targetFlags,
		const std::vector<std::string>& compileFlags, bool verbose);

	// Run an external tool directly (no shell), inheriting stdout/stderr so its output streams live.
	bool RunTool(const std::vector<std::string>& args);


	// Link the objects (the program's, the runtime's and foreign ones) and the libraries (.lib paths, bare names or
	// -l / -L arguments) into an executable with clang and lld.
	bool LinkWithLLD(const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
		const Core::String& outputPath, bool verbose);

	// Run an executable, streaming its output live and returning its exit code (no error noise - a
	// non-zero exit is expected for failing test runs). Returns false only if it couldn't be started.
	bool RunExecutable(const Core::String& exePath, int& outExitCode);

	// Remove a file if it exists (no-op on failure)
	void RemoveIfExists(const std::string& path);

	// Run an external tool, capturing stdout/stderr. Returns the exit code (-1 on spawn failure).
	bool RunToolWithOutput(const Core::Vector<Core::String>& args, int& exitCode,
		std::string& outStdout, std::string& outStderr);
}
