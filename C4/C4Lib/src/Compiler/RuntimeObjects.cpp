#include "Compiler/RuntimeObjects.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/FileIo.h"
#include "Compiler/ParallelFor.h"
#include "Compiler/Toolchain.h"
#include <cstdio>
#include <functional>
#include <algorithm>
#include <iostream>
#include <llvm/Support/Process.h>
#include <optional>
#include <system_error>
#include <thread>

namespace Scripting
{
	namespace
	{
		constexpr const char* kOptimization = "-O2";
		constexpr const char* kWindowsObjectFolder = "O2";
		constexpr const char* kLinuxObjectFolder = "linux-x64-O2";
		constexpr uint64_t kContentSeed = 14695981039346656037ull; // FNV-1a
		constexpr uint64_t kContentPrime = 1099511628211ull;

		// Objects are cached per install (keyed by its runtime folder), so two C4 installs never link each
		// other's objects, and an install folder that cannot be written still builds.
		fs::path ObjectFolder(const fs::path& runtime, TargetSystem system)
		{
			const std::string key = fs::absolute(runtime).lexically_normal().generic_string();
			char hex[17];
			std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)std::hash<std::string>{}(key));
			return fs::temp_directory_path() / "c4_runtime" / hex / (system == TargetSystem::Linux ? kLinuxObjectFolder : kWindowsObjectFolder);
		}

		// The files under `root` with one of `extensions`, in path order, as they read. Walked by the extended spelling,
		// so a deep install lists (BUG-356).
		std::vector<fs::path> FilesUnder(const fs::path& root, std::initializer_list<const char*> extensions)
		{
			std::vector<fs::path> files;
			std::error_code ec;
			for (fs::recursive_directory_iterator it(ExtendedPath(root), fs::directory_options::skip_permission_denied, ec), end;
				!ec && it != end; it.increment(ec))
				for (const char* extension : extensions)
					if (it->path().extension() == extension) files.push_back(PlainPath(it->path()));
			std::sort(files.begin(), files.end());
			return files;
		}

		std::string Bytes(const fs::path& file) { return ReadFileBytes(PathText(file)); }

		// `file`'s path from `root`, with forward slashes: what a source's object and the headers' key are named by.
		std::string RelativeName(const fs::path& file, const fs::path& root)
		{
			const std::u8string text = file.lexically_relative(PlainPath(ExtendedPath(root))).generic_u8string();
			return std::string(text.begin(), text.end());
		}

		// Every header's path and bytes, in path order: a changed header changes every source's key.
		uint64_t HeadersKey(const fs::path& root)
		{
			uint64_t key = kContentSeed;
			for (const fs::path& header : FilesUnder(root, { ".h" }))
				key = RuntimeContentKey(Bytes(header), RuntimeContentKey(RelativeName(header, root), key));
			return key;
		}

		// The headers the sources compile with: the MSVC and SDK kit's folder and setup's record of what it installed there
		// (none without a kit, for an installed Visual Studio's), or the Linux sysroot's and its record of the packages it
		// holds. Another kit or sysroot compiles every source again.
		uint64_t SysrootKey(const fs::path& sysroot, TargetSystem system, uint64_t seed)
		{
			if (sysroot.empty()) return seed;
			const char* record = system == TargetSystem::Linux ? "sysroot.txt" : ".setup.json";
			return RuntimeContentKey(Bytes(sysroot / record), RuntimeContentKey(sysroot.generic_string(), seed));
		}

		// The sysroot the runtime compiles against for `system`: nothing (with the reason printed) when a build for Linux
		// has none (LinuxSysrootForBuild).
		std::optional<fs::path> SysrootOf(TargetSystem system)
		{
			if (system == TargetSystem::Linux) return LinuxSysrootForBuild();
			return Toolchain::WindowsSysroot();
		}

		std::string ObjectStem(const std::string& relativeSource)
		{
			std::string stem = fs::path(relativeSource).replace_extension().generic_string();
			std::replace(stem.begin(), stem.end(), '/', '_');
			return stem;
		}

		// The cached objects of `stem` built from other bytes: no build links them again.
		void RemoveOtherVersions(const fs::path& objectDir, const std::string& stem, const fs::path& current)
		{
			std::error_code ec;
			for (fs::directory_iterator it(ExtendedPath(objectDir), ec), end; !ec && it != end; it.increment(ec))
			{
				const std::string name = PathText(it->path().filename());
				const bool sameSource = name.rfind(stem + "-", 0) == 0 && it->path().extension() == ".obj";
				std::error_code held;
				if (sameSource && it->path().filename() != current.filename()) fs::remove(it->path(), held); // another build may still hold it
			}
		}

		// Compiles into a private file first and renames it into place, so a parallel compiler process never
		// links a half-written object.
		bool Compile(const fs::path& root, const fs::path& source, const fs::path& object, TargetSystem system, const fs::path& sysroot,
			bool verbose)
		{
			const std::string clang = ClangPath();
			if (clang.empty()) return false;
			const fs::path partial = object.string() + "." + std::to_string(llvm::sys::Process::getProcessId()) + ".tmp";
			const std::vector<std::string> args = RuntimeCompileCommand(clang, root.string(), source.string(), partial.string(),
				system, sysroot.string());
			if (verbose)
			{
				std::string line = "Compiling C4 runtime:";
				for (const std::string& a : args) line += " " + a;
				std::fputs((line + "\n").c_str(), stdout); // one write: sources compile at once
			}
			if (!RunTool(args))
			{
				RemoveFile(PathText(partial));
				return false;
			}
			if (!RenameFile(PathText(partial), PathText(object))) return true;
			RemoveFile(PathText(partial));
			return FileExists(PathText(object)); // another process put its copy in place first
		}
	}

	std::string RuntimeFolder()
	{
		return Toolchain::Runtime().string();
	}

	bool RuntimeSourceIsFor(const std::string& relativeSource, TargetSystem system)
	{
		const std::string stem = fs::path(relativeSource).stem().string();
		auto named = [&stem](const char* suffix) {
			const size_t length = std::char_traits<char>::length(suffix);
			return stem.size() > length && stem.compare(stem.size() - length, length, suffix) == 0;
		};
		if (system == TargetSystem::Linux) return !named("Win32");
		return !named("Posix") && !named("Linux");
	}

	std::vector<std::string> RuntimeCompileCommand(const std::string& clang, const std::string& runtimeRoot,
		const std::string& source, const std::string& output, TargetSystem system, const std::string& sysroot)
	{
		std::vector<std::string> args = { clang, "-c", "-target", TargetTriple(system), kOptimization, "-Werror", "-I", runtimeRoot };
		if (system == TargetSystem::Linux)
		{
			args.insert(args.end(), { "-fPIC", "-D_GNU_SOURCE" });
			if (!sysroot.empty()) args.push_back("--sysroot=" + sysroot);
		}
		else
		{
			args.insert(args.end(), { "-mstackrealign", "-D_CRT_SECURE_NO_WARNINGS" });
			const std::vector<std::string> kit = WindowsSysrootFlags(sysroot);
			args.insert(args.end(), kit.begin(), kit.end());
		}
		if (fs::path(source).extension() == ".cpp") args.push_back("-std=c++20");
		args.push_back(source);
		args.push_back("-o");
		args.push_back(output);
		return args;
	}

	bool EnsureRuntimeObjects(bool verbose, TargetSystem system, Core::Vector<Core::String>& objects)
	{
		const fs::path root = Toolchain::Runtime();
		if (root.empty())
		{
			std::cerr << "error: the C4 runtime sources were not found: C4 looks in <C4 home>/C4Runtime (set C4_HOME to the "
				"folder that holds C4.exe, Bedrock/, C4Runtime/ and toolchain/)" << std::endl;
			return false;
		}
		const std::optional<fs::path> sysroot = SysrootOf(system);
		if (!sysroot) return false;
		const fs::path objectDir = ObjectFolder(root, system);
		CreateFolders(PathText(objectDir));
		const uint64_t headersKey = SysrootKey(*sysroot, system, HeadersKey(root));
		struct Missing
		{
			fs::path source;
			fs::path object;
			std::string stem;
		};
		std::vector<Missing> missing;
		for (const fs::path& source : FilesUnder(root, { ".c", ".cpp" }))
		{
			const std::string relative = RelativeName(source, root);
			if (!RuntimeSourceIsFor(relative, system)) continue;
			const fs::path object = objectDir / RuntimeObjectName(relative, RuntimeContentKey(Bytes(source), headersKey));
			if (!FileExists(PathText(object))) missing.push_back({ source, object, ObjectStem(relative) });
			objects.Push(Core::String(object.string().c_str()));
		}
		// A toolchain's first build compiles every source: one clang each, at once.
		std::vector<char> failed(missing.size(), 0);
		ParallelFor(missing.size(), std::max(1u, std::thread::hardware_concurrency()), [&](size_t i) {
			if (!Compile(root, missing[i].source, missing[i].object, system, *sysroot, verbose)) failed[i] = 1;
			else RemoveOtherVersions(objectDir, missing[i].stem, missing[i].object);
		});
		for (size_t i = 0; i < missing.size(); ++i)
			if (failed[i])
			{
				std::cerr << "error: failed to compile the C4 runtime source " << missing[i].source.string() << std::endl;
				return false;
			}
		return true;
	}

	uint64_t RuntimeContentKey(const std::string& bytes, uint64_t seed)
	{
		uint64_t key = seed;
		for (const unsigned char byte : bytes)
		{
			key ^= byte;
			key *= kContentPrime;
		}
		return key;
	}

	std::string RuntimeObjectName(const std::string& relativeSource, uint64_t key)
	{
		char hex[17];
		std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)key);
		return ObjectStem(relativeSource) + "-" + hex + ".obj";
	}
}
