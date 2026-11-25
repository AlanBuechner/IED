#include "Compiler/Toolchain.h"
#include "Compiler/FileIo.h"
#include "Target/TargetAbi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <llvm/Support/Program.h>
#include <cstdint>
#include <fstream>
#include <sys/stat.h>
#endif

namespace C4Compiler::Toolchain
{
	namespace
	{
		// A development build runs from build/bin/<config>/<project>/ inside the IED repository, whose C4 tree is C4/:
		// the repository is a few folders up. An installed C4 has everything under its home and never reaches this.
		constexpr int kRepositoryDepth = 4;
		const fs::path kSourceTree = "C4";

		// Through FileIo, so a C4 installed in a deep folder finds its parts (BUG-356).
		bool IsFile(const fs::path& p) { return C4Compiler::IsFile(PathText(p)); }
		bool IsFolder(const fs::path& p) { return C4Compiler::IsFolder(PathText(p)); }
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
		fs::path ProgramName(const char* name) { return fs::path(std::string(name) + TargetAbi::For(HostSystem()).ExecutableExtension()); }

		// A shared library's file name where the compiler runs, as Nuke's C/C++ driver names the ones it builds.
		fs::path LibraryName(const char* name) { return fs::path(TargetAbi::For(HostSystem()).SharedLibrary(name)); }

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

	namespace
	{
		std::string Hex(unsigned long long value)
		{
			char text[24];
			std::snprintf(text, sizeof(text), "%llx", value);
			return text;
		}

#ifdef _WIN32
		// A link's CodeView record ("RSDS", the .pdb's GUID and age): each link writes its own.
		std::string DebugRecord(const unsigned char* image, const IMAGE_NT_HEADERS& headers)
		{
			const IMAGE_DATA_DIRECTORY& directory = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
			const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(image + directory.VirtualAddress);
			const size_t count = directory.VirtualAddress ? directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY) : 0;
			for (size_t i = 0; i < count; ++i)
			{
				const IMAGE_DEBUG_DIRECTORY& entry = entries[i];
				if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 24) continue;
				const unsigned char* record = image + entry.AddressOfRawData;
				if (std::memcmp(record, "RSDS", 4) != 0) continue;
				std::string text;
				for (size_t b = 4; b < 24; ++b) text += Hex(record[b] | 0x100).substr(1); // the GUID's 16 bytes and the age's 4
				return text;
			}
			return {};
		}

		std::string HeaderIdentity(const unsigned char* image)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
			const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
			return Hex(headers->FileHeader.TimeDateStamp) + "-" + Hex(headers->OptionalHeader.SizeOfImage) + "-" +
				Hex(headers->OptionalHeader.AddressOfEntryPoint) + "-" + Hex(headers->OptionalHeader.CheckSum) + "-" + DebugRecord(image, *headers);
		}
#else
		std::string StatIdentity(const struct stat& file)
		{
			return Hex(file.st_dev) + "-" + Hex(file.st_ino) + "-" + Hex(static_cast<unsigned long long>(file.st_size)) + "-" +
				Hex(static_cast<unsigned long long>(file.st_mtim.tv_sec)) + "." + Hex(static_cast<unsigned long long>(file.st_mtim.tv_nsec));
		}

		// The mapping of /proc/self/maps that holds `address`: the file it maps, as it was when it was mapped.
		std::string MappedIdentity(uintptr_t address)
		{
			std::ifstream maps("/proc/self/maps");
			std::string line;
			while (std::getline(maps, line))
			{
				unsigned long long start = 0, end = 0, offset = 0, inode = 0;
				char permissions[8] = {}, device[32] = {};
				if (std::sscanf(line.c_str(), "%llx-%llx %7s %llx %31s %llu", &start, &end, permissions, &offset, device, &inode) != 6) continue;
				if (address < start || address >= end) continue;
				struct stat file = {};
				const std::string mapped = "/proc/self/map_files/" + Hex(start) + "-" + Hex(end);
				if (stat(mapped.c_str(), &file) == 0) return StatIdentity(file);
				return std::string(device) + "-" + Hex(inode); // a kernel that does not show the mapped files: its inode
			}
			return {};
		}
#endif
	}

	std::string ImageIdentity(const void* address)
	{
#ifdef _WIN32
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(address), &module))
			return {};
		return HeaderIdentity(reinterpret_cast<const unsigned char*>(module));
#else
		return MappedIdentity(reinterpret_cast<uintptr_t>(address));
#endif
	}

	std::string RunningImageIdentity()
	{
#ifdef _WIN32
		return HeaderIdentity(reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr)));
#else
		struct stat running = {};
		return stat("/proc/self/exe", &running) == 0 ? StatIdentity(running) : std::string(); // the running file, renamed or not
#endif
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

	fs::path LdLld()
	{
		const fs::path clang = Clang();
		const fs::path lld = clang.empty() ? fs::path() : clang.parent_path() / ProgramName("ld.lld");
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
