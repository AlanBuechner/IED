#include "Compiler/BuildUtils.h"
#include "Compiler/Version.h"
#include "Compiler/FileIo.h"
#include "Compiler/Toolchain.h"
#include "Core/Core.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <cctype>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

namespace C4Compiler
{

	namespace
	{
		using Apply = void (*)(CompileOptions&, const std::string&);

		// One command-line option: its spellings, the name of its value (null for a flag), its help line and what
		// it sets. The table drives both the parser and the usage text, so -h always lists every option.
		struct Option
		{
			std::vector<const char*> names;
			const char* value;
			const char* help;
			Apply apply;
			bool attached = false; // `-DNAME`, `-O2`, `-lname`: the value may follow the name directly
		};

		constexpr unsigned kMaxCount = 64;
		constexpr unsigned kMaxPartitions = 256; // as ProgramPlacement's
		constexpr unsigned kMaxIdleSeconds = 7 * 24 * 60 * 60; // a week

		// A count of threads or objects, 1 to `most`; 0 with the error recorded.
		unsigned ParseCount(const std::string& v, const char* option, CompileOptions& o, unsigned most = kMaxCount)
		{
			char* end = nullptr;
			const unsigned long count = std::strtoul(v.c_str(), &end, 10);
			if (!v.empty() && std::isdigit(static_cast<unsigned char>(v[0])) && *end == '\0' && count >= 1 && count <= most)
				return static_cast<unsigned>(count);
			o.error = std::string("'") + option + "' needs a count from 1 to " + std::to_string(most);
			return 0;
		}

		void ParseSubsystem(const std::string& v, CompileOptions& o)
		{
			if (v == "console") o.subsystem = Subsystem::Console;
			else if (v == "windows") o.subsystem = Subsystem::Windows;
			else o.error = "--subsystem takes console or windows, not '" + v + "'";
		}

		// A compiled resource file (llvm-rc or rc.exe output) goes to the linker as it is.
		bool IsResourceFile(const std::string& v) { return fs::path(v).extension() == ".res"; }

		bool EndsWithIgnoringCase(const std::string& text, const std::string& suffix)
		{
			return text.size() >= suffix.size() && std::equal(suffix.begin(), suffix.end(), text.end() - suffix.size(),
				[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
		}

		const std::vector<Option>& Options()
		{
			static const std::vector<Option> options = {
				{ { "-o" }, "<path>", "Output executable (default output.exe, or output for Linux)", [](CompileOptions& o, const std::string& v) { o.outputFile = v; } },
				{ { "-I" }, "<dir>", "A project this one builds with: its sources compile and its headers resolve", [](CompileOptions& o, const std::string& v) {
					o.includeDirs.Push(v); o.inputs.Push(v); }, true },
				{ { "--projects" }, "<file>", "Build the projects a description names, each with its own settings", [](CompileOptions& o, const std::string& v) {
					o.projectsFile = v; } },
				{ { "-D" }, "<name>[=<value>]", "Define a macro for every file (no value: 1)", [](CompileOptions& o, const std::string& v) {
					o.defines.push_back(ParseMacroDefine(v)); }, true },
				{ { "-O" }, "<n>", "Optimization level of the generated code, 0 to 3 (default 0)", [](CompileOptions& o, const std::string& v) {
					o.optLevel = v.size() == 1 && v[0] >= '0' && v[0] <= '3' ? v[0] - '0' : -1; }, true },
				{ { "-j", "--threads" }, "<n>", "Threads that lower function bodies and generate code, 1 to 64 (default: the processors)",
					[](CompileOptions& o, const std::string& v) { o.threads = ParseCount(v, "-j", o); }, true },
				{ { "--partitions" }, "<n>", "Assemble the program into <n> objects, 1 to 256 (default: by its size)",
					[](CompileOptions& o, const std::string& v) { o.partitions = ParseCount(v, "--partitions", o, kMaxPartitions); } },
				{ { "--incremental" }, nullptr, "Optimize each object alone: an edit reoptimizes only the objects it changes, inlining across objects only small functions",
					[](CompileOptions& o, const std::string&) { o.incremental = true; } },
				{ { "--target" }, "<windows-x64|linux-x64>", "The system the program runs on (default: the one the compiler runs on)",
					[](CompileOptions& o, const std::string& v) { o.targetSystem = v; } },
				{ { "-march=" }, "<cpu>", "The processor to compile for: x86-64 (default), x86-64-v2/v3/v4, native or an LLVM CPU name",
					[](CompileOptions& o, const std::string& v) { o.targetFlags.push_back("-march=" + v); }, true },
				{ { "-m" }, "<feature>", "Turn a processor feature on (-mavx2) or off (-mno-avx2): sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma f16c bmi bmi2 lzcnt avx512f avx512vl avx512bw avx512dq avx512cd",
					[](CompileOptions& o, const std::string& v) { o.targetFlags.push_back("-m" + v); }, true },
				{ { "-fno-strict-aliasing" }, nullptr, "Any load or store may touch any memory: no type-based alias information (D-64)",
					[](CompileOptions& o, const std::string&) { o.strictAliasing = false; } },
				{ { "-fstack-maps" }, nullptr, "The collector reads each C4 frame's pointers from stack maps, not the whole frame: a stale address in a dead slot keeps nothing alive",
					[](CompileOptions& o, const std::string&) { o.stackMaps = true; } },
				{ { "-fno-stack-maps" }, nullptr, "The collector reads every frame whole (the default)",
					[](CompileOptions& o, const std::string&) { o.stackMaps = false; } },
				{ { "--int-dir" }, "<dir>", "Write the intermediates (.obj, .ll, .bc) here, not next to the output", [](CompileOptions& o, const std::string& v) {
					o.intermediateDir = v; } },
				{ { "-l" }, "<lib>", "Link the library <lib> (<lib>.lib), or a compiled resource file (<file>.res: icons, version information)",
					[](CompileOptions& o, const std::string& v) { o.linkArguments.Push(IsResourceFile(v) ? v : "-l" + v); }, true },
				{ { "-L" }, "<dir>", "Look for -l libraries in <dir>", [](CompileOptions& o, const std::string& v) { o.linkArguments.Push("-L" + v); }, true },
				{ { "--subsystem" }, "<console|windows>", "The executable's subsystem: console (default), or windows for a windowed program that gets no console window (main is still its entry)",
					[](CompileOptions& o, const std::string& v) { ParseSubsystem(v, o); } },
				{ { "-g" }, nullptr, "Debug information: CodeView in the object and a .pdb beside the executable", [](CompileOptions& o, const std::string&) {
					o.debugInfo = true; } },
				{ { "-c" }, nullptr, "Compile to an object (<output base>.obj) and stop: no runtime, no link", [](CompileOptions& o, const std::string&) { o.compileOnly = true; } },
				{ { "-bc", "--emit-bitcode" }, nullptr, "Write LLVM bitcode (<output base>.bc) and stop", [](CompileOptions& o, const std::string&) { o.emitBitcode = true; } },
				{ { "-S", "--emit-ir" }, nullptr, "Write LLVM IR (<output base>.ll) and stop", [](CompileOptions& o, const std::string&) { o.emitIR = true; } },
				{ { "-test", "--test" }, nullptr, "Build the [test] harness instead of main", [](CompileOptions& o, const std::string&) { o.runTests = true; } },
				{ { "-test-json", "--test-json" }, "<path>", "With -test: the run writes a JSON summary to <path>", [](CompileOptions& o, const std::string& v) {
					o.testJsonPath = v; } },
				{ { "-reflect-tag", "--reflect-tag" }, "<tag>", "Register reflection under <tag> (a hot-reload source)", [](CompileOptions& o, const std::string& v) {
					o.reflectTag = v; } },
				{ { "--script-host" }, nullptr, "Build a script host: a symbol table scripts link against, and <out>.c4i beside the output",
					[](CompileOptions& o, const std::string&) { o.scriptHost = true; } },
				{ { "-gc-stats", "--gc-stats" }, nullptr, "The program prints the GC's statistics to stderr after each collection", [](CompileOptions& o, const std::string&) { o.gcStats = true; } },
				{ { "-Werror" }, nullptr, "Every warning is an error", [](CompileOptions& o, const std::string&) { o.warningsAsErrors = true; } },
				{ { "-w" }, nullptr, "Do not report warnings", [](CompileOptions& o, const std::string&) { o.noWarnings = true; } },
				{ { "-export-macros", "--export-macros" }, nullptr, "Write each file's final macros as <name>.c4macro next to the output",
					[](CompileOptions& o, const std::string&) { o.exportMacros = true; } },
				{ { "-ftime", "--time-phases" }, nullptr, "Print how long each phase took", [](CompileOptions& o, const std::string&) { o.timePhases = true; } },
				{ { "--pretty-print" }, nullptr, "Print the parsed syntax trees", [](CompileOptions& o, const std::string&) { o.prettyPrint = true; } },
				{ { "-v" }, nullptr, "Print the tool command lines (clang, CBind, link)", [](CompileOptions& o, const std::string&) { o.verbose = true; } },
				{ { "--serve" }, nullptr, "Stay alive: each 'build' line on stdin builds again (reusing the parses of unchanged files), answered by 'done <exit code>'; 'quit' or the end of stdin ends it",
					[](CompileOptions& o, const std::string&) { o.serve = true; } },
				{ { "--serve-on" }, "<name>", "A build server: each client of the pipe (a socket on Linux) <name> sends 'build <stamp>' and gets the build's output, then 'done <exit code>'",
					[](CompileOptions& o, const std::string& v) { o.serveOn = v; } },
				{ { "--serve-stamp" }, "<text>", "With --serve-on: a request with another stamp gets 'stale', and the server ends (its compiler was rebuilt)",
					[](CompileOptions& o, const std::string& v) { o.serveStamp = v; } },
				{ { "--serve-idle" }, "<seconds>", "With --serve-on: the server ends after <seconds> without a build (default 1800)",
					[](CompileOptions& o, const std::string& v) { o.serveIdleSeconds = ParseCount(v, "--serve-idle", o, kMaxIdleSeconds); } },
				{ { "--version" }, nullptr, "Print the compiler version", [](CompileOptions& o, const std::string&) { o.printVersion = true; } },
				{ { "-h", "--help" }, nullptr, "Print this help", [](CompileOptions& o, const std::string&) { o.printHelp = true; } },
			};
			return options;
		}

		// The option `arg` names, exactly or (for an attached one) as its prefix; `value` gets an attached value.
		const Option* Find(const std::string& arg, std::string& value)
		{
			for (const Option& option : Options())
				for (const char* name : option.names)
				{
					if (arg == name) return &option;
					if (option.attached && arg.size() > std::strlen(name) && arg.starts_with(name)) { value = arg.substr(std::strlen(name)); return &option; }
				}
			return nullptr;
		}

		// What the command line leaves open: the thread count follows the processors; -c writes one object.
		void ResolveCodeGeneration(CompileOptions& opts, unsigned processors)
		{
			if (opts.threads == 0) opts.threads = std::clamp(processors, 1u, kMaxCount);
			if (opts.compileOnly && opts.partitions > 1) opts.error = "--partitions needs a link: -c writes one object";
			if (opts.compileOnly && opts.incremental) opts.error = "--incremental needs a link: -c writes one object";
		}

		// The target --target and the -march / -m flags select (D-62, D-86), or the reason they select none. A program
		// for Linux is named without .exe unless -o names it.
		void ResolveTarget(CompileOptions& opts)
		{
			TargetSystem system = HostSystem();
			if (!opts.targetSystem.empty())
			{
				const std::optional<TargetSystem> named = TargetFromName(opts.targetSystem);
				if (!named)
				{
					opts.error = "--target takes " + TargetNames() + ", not '" + opts.targetSystem + "'";
					return;
				}
				system = *named;
			}
			std::string why;
			const std::optional<CpuTarget> target = CpuTarget::FromFlags(opts.targetFlags, why);
			if (!target)
			{
				opts.error = why;
				return;
			}
			opts.target = target->On(system);
			if (opts.outputFile.empty()) opts.outputFile = Core::String("output") + opts.target.Abi().ExecutableExtension();
			// A Linux script host links the script host library built for Linux, which only a compiler running there has.
			if (system == TargetSystem::Linux && opts.scriptHost && HostSystem() != TargetSystem::Linux)
				opts.error = "--script-host for linux-x64 needs the Linux script host library: build the program with the compiler on Linux";
		}
	}

	void PrintUsage()
	{
		std::cout << "usage: C4.exe [options] <folder or file>..." << std::endl << std::endl << "options:" << std::endl;
		for (const Option& option : Options())
		{
			std::string spelled;
			for (const char* name : option.names) spelled += (spelled.empty() ? "" : ", ") + std::string(name);
			if (option.value) spelled += std::string(" ") + option.value;
			std::cout << "  " << spelled << std::string(spelled.size() < 34 ? 34 - spelled.size() : 1, ' ') << option.help << std::endl;
		}
	}

	CompileOptions ParseArgs(int argc, char** argv)
	{
		CompileOptions opts;
		for (int i = 1; i < argc && opts.error.empty(); ++i)
		{
			const std::string arg = argv[i];
			if (arg.empty() || arg[0] != '-') { opts.inputs.Push(arg); continue; }
			std::string value;
			const Option* option = Find(arg, value);
			if (!option) { opts.error = "unknown option '" + arg + "' (C4.exe -h lists the options)"; break; }
			if (option->value && value.empty())
			{
				if (i + 1 >= argc) { opts.error = "'" + arg + "' needs a value: " + option->value; break; }
				value = argv[++i];
			}
			option->apply(opts, value);
			if (opts.optLevel < 0) opts.error = "'" + arg + "' needs an optimization level from 0 to 3";
		}
		if (opts.error.empty()) ResolveCodeGeneration(opts, std::thread::hardware_concurrency());
		if (opts.error.empty()) ResolveTarget(opts);
		return opts;
	}

	fs::path IntermediateFolder(const CompileOptions& options)
	{
		if (!options.intermediateDir.empty()) return fs::path(options.intermediateDir.c_str());
		return fs::path(options.outputFile.c_str()).parent_path();
	}

	std::string ClangPath()
	{
		const fs::path clang = Toolchain::Clang();
		if (clang.empty())
			std::cerr << "error: clang.exe was not found: C4 looks in <C4 home>/toolchain/bin (set C4_HOME to the folder that "
				"holds C4.exe, Bedrock/, C4Runtime/ and toolchain/)" << std::endl;
		return clang.string();
	}

	std::vector<std::string> LinkerArguments(const std::string& argument)
	{
		if (argument.rfind("-Wl,", 0) == 0)
		{
			std::vector<std::string> split;
			for (size_t from = 4; from <= argument.size();)
			{
				const size_t found = argument.find(',', from);
				const size_t comma = found == std::string::npos ? argument.size() : found;
				if (comma > from) split.push_back(argument.substr(from, comma - from));
				from = comma + 1;
			}
			return split;
		}
		if (argument.rfind("-l", 0) == 0)
		{
			const std::string name = argument.substr(2);
			return { EndsWithIgnoringCase(name, ".lib") ? name : name + ".lib" };
		}
		if (argument.rfind("-L", 0) == 0) return { "-libpath:" + argument.substr(2) };
		return { argument };
	}

	std::string LinuxLinkerArgument(const std::string& argument)
	{
		if (argument.rfind("-l", 0) != 0) return argument;
		const std::string name = argument.substr(2);
		const bool file = name.find('/') != std::string::npos || name.find('\\') != std::string::npos;
		return file ? name : argument;
	}

	bool CompileForeignSource(const Core::String& srcPath, const Core::String& objPath,
		const Core::String& language, const Core::Vector<Core::String>& defines,
		const Core::Vector<Core::String>& includeDirs, TargetSystem system, const std::vector<std::string>& targetFlags,
		const std::vector<std::string>& compileFlags, bool verbose)
	{
		const std::string clangPath = ClangPath();
		if (clangPath.empty()) return false;

		const TargetAbi& target = TargetAbi::For(system);
		const std::optional<fs::path> sysroot = target.Sysroot();
		if (!sysroot) return false;
		std::vector<std::string> args = { clangPath, "-c", "-target", target.Triple(), "-MD", "-MF", std::string(objPath.c_str()) + ".d" };
		const std::vector<std::string> flags = target.ForeignSourceFlags(*sysroot);
		args.insert(args.end(), flags.begin(), flags.end());
		args.insert(args.end(), targetFlags.begin(), targetFlags.end());
		args.insert(args.end(), compileFlags.begin(), compileFlags.end());
		args.push_back(language == "c++" ? "-std=c++20" : "-std=c11");

		for (const auto& d : defines)
		{
			std::string flag = "-D";
			if (!d.empty() && d.find('=') != std::string::npos)
				flag += d;
			else
				flag += d + "=1";
			args.push_back(flag);
		}
		for (const auto& inc : includeDirs)
		{
			args.push_back("-I");
			args.push_back(inc);
		}

		args.push_back("-x");
		args.push_back(language == "c++" ? "c++" : "c");
		args.push_back(srcPath);
		args.push_back("-o");
		args.push_back(objPath);

		if (verbose)
		{
			std::cout << "Compiling foreign source: ";
			for (const auto& a : args) std::cout << a << " ";
			std::cout << std::endl;
		}

		return RunTool(args);
	}

	void RemoveIfExists(const std::string& path) { RemoveFile(path); }

} // namespace C4Compiler
