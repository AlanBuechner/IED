#include "Compiler/Toolchain.h"
#include "Compiler/FileIo.h"
#include "Compiler/TargetSystem.h"
#include <cstdlib>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <llvm/Support/Program.h>
#endif

namespace Scripting::Toolchain
{
	namespace
	{
		// A development build runs from build/bin/<config>/<project>/ inside the IED repository, whose C4 tree is C4/:
		// the repository is a few folders up. An installed C4 has everything under its home and never reaches this.
		constexpr int kRepositoryDepth = 4;
		const fs::path kSourceTree = "C4";

		// Through FileIo, so a C4 installed in a deep folder finds its parts (BUG-356).
		bool IsFile(const fs::path& p) { return Scripting::IsFile(PathText(p)); }
		bool IsFolder(const fs::path& p) { return Scripting::IsFolder(PathText(p)); }
		bool Exists(const fs::path& p) { return FileExists(PathText(p)); }

		fs::path ExecutableFolder()
		{
			const fs::path executable = Executable();
			return executable.empty() ? fs::current_path() : executable.parent_path();
		}

		// The C4 tree of a repository folder: it holds the compiler's sources and the runtime, which tells it from the
		// compiler's own bin folder (build/bin/<config>/C4/, with copies of the runtime and Bedrock).
		bool IsSourceTree(const fs::path& folder) { return IsFolder(folder / "C4Lib") && IsFolder(folder / "C4Runtime"); }

		// `relative` in the nearest folder above the executable that has it, or in that folder's C4 tree, for a
		// development build.
		fs::path InRepository(const fs::path& relative)
		{
			fs::path folder = ExecutableFolder();
			for (int i = 0; i < kRepositoryDepth && !folder.empty(); ++i)
			{
				folder = folder.parent_path();
				if (Exists(folder / relative)) return folder / relative;
				if (IsSourceTree(folder / kSourceTree) && Exists(folder / kSourceTree / relative)) return folder / kSourceTree / relative;
			}
			return {};
		}

		// A program's file name where the compiler runs: `clang.exe` on Windows, `clang` on Linux.
		fs::path ProgramName(const char* name) { return fs::path(std::string(name) + ExecutableExtension(HostSystem())); }

		// A shared library's file name where the compiler runs, as Nuke's C/C++ driver names the ones it builds.
		fs::path LibraryName(const char* name) { return fs::path(std::string(name) + (HostSystem() == TargetSystem::Windows ? ".dll" : ".so")); }

#ifndef _WIN32
		// The clang of an LLVM release outside C4's home, for a compiler that runs on Linux (the checkout's vendor/llvm is
		// Windows'): $LLVM_HOME's, else the one on PATH with its links followed, so its resource folder is found above it.
		fs::path SystemClang()
		{
			if (const char* llvm = std::getenv("LLVM_HOME"); llvm && *llvm && IsFile(fs::path(llvm) / "bin" / "clang"))
				return fs::path(llvm) / "bin" / "clang";
			const llvm::ErrorOr<std::string> found = llvm::sys::findProgramByName("clang");
			if (!found) return {};
			std::error_code ec;
			const fs::path resolved = fs::canonical(*found, ec);
			return ec ? fs::path(*found) : resolved;
		}
#endif
	}

	fs::path Executable()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) return fs::path(buffer);
#else
		std::error_code ec;
		const fs::path self = fs::read_symlink("/proc/self/exe", ec);
		if (!ec) return self;
#endif
		return {};
	}

	fs::path Home()
	{
		if (const char* home = std::getenv("C4_HOME"); home && *home && IsFolder(home)) return fs::path(home);
		return ExecutableFolder();
	}

	fs::path Clang()
	{
		const fs::path installed = Home() / "toolchain" / "bin" / ProgramName("clang");
		if (IsFile(installed)) return installed;
#ifdef _WIN32
		return InRepository(fs::path("vendor") / "llvm" / "bin" / "clang.exe");
#else
		return SystemClang();
#endif
	}

	fs::path LldLink()
	{
		const fs::path clang = Clang();
		const fs::path lld = clang.empty() ? fs::path() : clang.parent_path() / ProgramName("lld-link");
		return !lld.empty() && IsFile(lld) ? lld : fs::path();
	}

	fs::path ClangResourceDirectory()
	{
		const fs::path clang = Clang();
		if (clang.empty()) return {};
		std::error_code ec;
		for (const fs::directory_entry& version : fs::directory_iterator(ExtendedPath(clang.parent_path().parent_path() / "lib" / "clang"), ec))
			if (const fs::path folder = PlainPath(version.path()); IsFolder(folder / "include")) return folder;
		return {};
	}

	fs::path ClangRuntimeLibraries()
	{
		const fs::path clang = Clang();
		if (clang.empty()) return {};
		std::error_code ec;
		for (const fs::directory_entry& version : fs::directory_iterator(ExtendedPath(clang.parent_path().parent_path() / "lib" / "clang"), ec))
			if (const fs::path windows = PlainPath(version.path()) / "lib" / "windows"; IsFolder(windows)) return windows;
		return {};
	}

	fs::path WindowsSysroot()
	{
		const fs::path toolsets = InRepository(fs::path("vendor") / "msvc" / "VC" / "Tools" / "MSVC");
		return toolsets.empty() ? fs::path() : toolsets.parent_path().parent_path().parent_path();
	}

	fs::path LinuxSysroot()
	{
		const fs::path sysroot = fs::path("sysroots") / "linux-x64";
		if (IsFile(Home() / sysroot / "sysroot.txt")) return Home() / sysroot;
		const fs::path vendored = InRepository(fs::path("vendor") / sysroot / "sysroot.txt");
		return vendored.empty() ? fs::path() : vendored.parent_path();
	}

	fs::path Runtime()
	{
		const fs::path installed = Home() / "C4Runtime";
		if (IsFolder(installed)) return installed;
		return InRepository("C4Runtime");
	}

	fs::path CBind()
	{
		const fs::path installed = Home() / "CBind" / ProgramName("CBind");
		if (IsFile(installed)) return installed;
		const fs::path sibling = ExecutableFolder().parent_path() / "CBind" / ProgramName("CBind");
		return IsFile(sibling) ? sibling : fs::path();
	}

	fs::path ScriptHost()
	{
		const fs::path library = LibraryName("C4Script");
		const fs::path installed = Home() / "C4Script";
		if (IsFile(installed / library)) return installed;
		const fs::path sibling = ExecutableFolder().parent_path() / "C4Script";
		return IsFile(sibling / library) ? sibling : fs::path();
	}

	fs::path Bedrock()
	{
		// The library is the folder holding bedrock.ch, not any folder of that name: a build's output folder of the
		// workspace's Bedrock project (build/bin/<config>/Bedrock, beside a development build's programs) is none.
		const fs::path installed = Home() / "Bedrock";
		if (IsFile(installed / "bedrock.ch")) return installed;
		const fs::path repository = InRepository(fs::path("Bedrock") / "bedrock.ch");
		return repository.empty() ? installed : repository.parent_path();
	}
}
