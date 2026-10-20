#include "Compiler/BuildUtils.h"
#include "Compiler/Version.h"
#include "Compiler/Toolchain.h"
#include "Core/Core.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <windows.h>

namespace Scripting
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

		const std::vector<Option>& Options()
		{
			static const std::vector<Option> options = {
				{ { "-o" }, "<path>", "Output executable (default output.exe)", [](CompileOptions& o, const std::string& v) { o.outputFile = v; } },
				{ { "-I" }, "<dir>", "A project this one builds with: its sources compile and its headers resolve", [](CompileOptions& o, const std::string& v) {
					o.includeDirs.Push(v); o.inputs.Push(v); }, true },
				{ { "--projects" }, "<file>", "Build the projects a description names, each with its own settings", [](CompileOptions& o, const std::string& v) {
					o.projectsFile = v; } },
				{ { "-D" }, "<name>[=<value>]", "Define a macro for every file (no value: 1)", [](CompileOptions& o, const std::string& v) {
					o.defines.push_back(ParseMacroDefine(v)); }, true },
				{ { "-O" }, "<n>", "Optimization level of the generated code, 0 to 3 (default 0)", [](CompileOptions& o, const std::string& v) {
					o.optLevel = v.size() == 1 && v[0] >= '0' && v[0] <= '3' ? v[0] - '0' : -1; }, true },
				{ { "-march=" }, "<cpu>", "The processor to compile for: x86-64 (default), x86-64-v2/v3/v4, native or an LLVM CPU name",
					[](CompileOptions& o, const std::string& v) { o.targetFlags.push_back("-march=" + v); }, true },
				{ { "-m" }, "<feature>", "Turn a processor feature on (-mavx2) or off (-mno-avx2): sse3 ssse3 sse4.1 sse4.2 popcnt avx avx2 fma f16c bmi bmi2 lzcnt avx512f avx512vl avx512bw avx512dq avx512cd",
					[](CompileOptions& o, const std::string& v) { o.targetFlags.push_back("-m" + v); }, true },
				{ { "-fno-strict-aliasing" }, nullptr, "Any load or store may touch any memory: no type-based alias information (D-64)",
					[](CompileOptions& o, const std::string&) { o.strictAliasing = false; } },
				{ { "--int-dir" }, "<dir>", "Write the intermediates (.ll, .obj) here, not next to the output", [](CompileOptions& o, const std::string& v) {
					o.intermediateDir = v; } },
				{ { "-l" }, "<lib>", "Link the library <lib> (<lib>.lib)", [](CompileOptions& o, const std::string& v) { o.linkArguments.Push("-l" + v); }, true },
				{ { "-L" }, "<dir>", "Look for -l libraries in <dir>", [](CompileOptions& o, const std::string& v) { o.linkArguments.Push("-L" + v); }, true },
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

		// The target the -march / -m flags select (D-62), or the reason they select none.
		void ResolveTarget(CompileOptions& opts)
		{
			std::string why;
			if (std::optional<CpuTarget> target = CpuTarget::FromFlags(opts.targetFlags, why)) opts.target = *target;
			else opts.error = why;
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
		if (opts.error.empty()) ResolveTarget(opts);
		return opts;
	}

	std::string ClangPath()
	{
		const fs::path clang = Toolchain::Clang();
		if (clang.empty())
			std::cerr << "error: clang.exe was not found: C4 looks in <C4 home>/toolchain/bin (set C4_HOME to the folder that "
				"holds C4.exe, Bedrock/, C4Runtime/ and toolchain/)" << std::endl;
		return clang.string();
	}

	bool RunTool(const std::vector<std::string>& args)
	{
		std::wstring cmdLine;
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (i > 0) cmdLine += L" ";
			cmdLine += L"\"";
			for (unsigned char c : args[i])
				cmdLine += (wchar_t)c;
			cmdLine += L"\"";
		}

		std::wstring mutableCmd = cmdLine;

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
		{
			std::cerr << "error: Failed to start process (code " << GetLastError() << ")" << std::endl;
			return false;
		}

		CloseHandle(pi.hThread);
		DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
		if (waitResult == WAIT_FAILED)
		{
			std::cerr << "error: Failed to wait for process" << std::endl;
			CloseHandle(pi.hProcess);
			return false;
		}

		DWORD exitCode = 0;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);

		if (exitCode != 0)
		{
			std::cerr << "error: Process failed with exit code " << exitCode << std::endl;
			return false;
		}
		return true;
	}

	// Run an executable, streaming its output live and returning its exit code (no error noise - a
	// non-zero exit is expected for failing test runs). Returns false only if it couldn't be started.
	bool RunExecutable(const Core::String& exePath, int& outExitCode)
	{
		std::wstring cmdLine = L"\"" + std::wstring(exePath.begin(), exePath.end()) + L"\"";
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(nullptr, const_cast<wchar_t*>(cmdLine.data()), nullptr, nullptr, TRUE, 0,
			nullptr, nullptr, &si, &pi))
		{
			std::cerr << "error: Failed to start test executable (code " << GetLastError() << ")" << std::endl;
			return false;
		}
		CloseHandle(pi.hThread);
		DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD exitCode = 0;
		if (waitResult != WAIT_FAILED)
			GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		outExitCode = (int)exitCode;
		return true;
	}

	bool CompileForeignSource(const Core::String& srcPath, const Core::String& objPath,
		const Core::String& language, const Core::Vector<Core::String>& defines,
		const Core::Vector<Core::String>& includeDirs, const std::vector<std::string>& targetFlags, bool verbose)
	{
		const std::string clangPath = ClangPath();
		if (clangPath.empty()) return false;

		std::vector<std::string> args = {
			clangPath, "-c", "-target", "x86_64-pc-windows-msvc", "-mstackrealign"
		};
		args.insert(args.end(), targetFlags.begin(), targetFlags.end());
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

	bool LinkWithLLD(const Core::String& objPath, const Core::Vector<Core::String>& extraObjects,
		const Core::Vector<Core::String>& libraries, const Core::String& outputPath, bool verbose)
	{
		const std::string clangPath = ClangPath();
		if (clangPath.empty()) return false;

		// Link directly with clang - it handles CRT discovery and MSVC import libs correctly.
		std::vector<std::string> args = {
			clangPath, "-target", "x86_64-pc-windows-msvc", "-fuse-ld=lld",
			objPath
		};
		for (const auto& extra : extraObjects)
			args.push_back(extra);
		for (const auto& lib : libraries)
			args.push_back(lib.c_str());

		args.push_back("-o");
		args.push_back(outputPath);
		args.push_back("-mstackrealign");
		args.push_back("-Wl,/SUBSYSTEM:CONSOLE");

		if (verbose)
		{
			std::cout << "Linking: ";
			for (const auto& a : args) std::cout << a << " ";
			std::cout << std::endl;
		}

		return RunTool(args);
	}

	void RemoveIfExists(const std::string& path)
	{
		if (fs::exists(path))
			fs::remove(path);
	}

	bool RunToolWithOutput(const Core::Vector<Core::String>& args, int& exitCode,
		std::string& outStdout, std::string& outStderr)
	{
		outStdout.clear();
		outStderr.clear();
		exitCode = -1;

		HANDLE outRead = INVALID_HANDLE_VALUE, outWrite = INVALID_HANDLE_VALUE;
		HANDLE errRead = INVALID_HANDLE_VALUE, errWrite = INVALID_HANDLE_VALUE;
		SECURITY_ATTRIBUTES sa = {};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		if (!CreatePipe(&outRead, &outWrite, &sa, 0)) { outStderr = "FAIL pipe1"; return false; }
		if (!CreatePipe(&errRead, &errWrite, &sa, 0)) { CloseHandle(outRead); CloseHandle(outWrite); outStderr = "FAIL pipe2"; return false; }

		std::wstring cmdLine;
		for (size_t i = 0; i < args.Count(); ++i)
		{
			if (i > 0) cmdLine += L" ";
			cmdLine += L"\"";
			for (unsigned char c : std::string(args[i].c_str()))
				cmdLine += (wchar_t)c;
			cmdLine += L"\"";
		}
		std::wstring mutableCmd = cmdLine;

		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = outWrite;
		si.hStdError = errWrite;

		PROCESS_INFORMATION pi = {};
		DWORD spawnErr = 0;
		if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &pi))
		{
			spawnErr = GetLastError();
			CloseHandle(outRead); CloseHandle(outWrite);
			CloseHandle(errRead); CloseHandle(errWrite);
			std::string narrow;
			for (wchar_t c : cmdLine) narrow += (char)c;
			outStderr = "CreateProcessW failed (Win32 error " + std::to_string(spawnErr) + ") cmd=" + narrow;
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(outWrite);
		CloseHandle(errWrite);

		auto readAll = [](HANDLE h, std::string& out) {
			char buf[4096]; DWORD n = 0;
			while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0)
				out.append(buf, n);
		};
		readAll(outRead, outStdout);
		readAll(errRead, outStderr);
		CloseHandle(outRead);
		CloseHandle(errRead);

		DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 0;
		if (waitResult != WAIT_FAILED)
			GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		exitCode = static_cast<int>(code);
		return true;
	}

} // namespace Scripting
