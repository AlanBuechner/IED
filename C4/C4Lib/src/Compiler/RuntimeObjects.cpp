#include "Compiler/RuntimeObjects.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/FileIo.h"
#include "Compiler/ParallelFor.h"
#include "Compiler/Toolchain.h"
#include <cstdio>
#include <functional>
#include <algorithm>
#include <iostream>
#include <system_error>
#include <thread>
#include <process.h>

namespace Scripting
{
	namespace
	{
		constexpr const char* kOptimization = "-O2";
		constexpr const char* kObjectFolder = "O2";
		constexpr uint64_t kContentSeed = 14695981039346656037ull; // FNV-1a
		constexpr uint64_t kContentPrime = 1099511628211ull;

		// Objects are cached per install (keyed by its runtime folder), so two C4 installs never link each
		// other's objects, and an install folder that cannot be written still builds.
		fs::path ObjectFolder(const fs::path& runtime)
		{
			const std::string key = fs::absolute(runtime).lexically_normal().generic_string();
			char hex[17];
			std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)std::hash<std::string>{}(key));
			return fs::temp_directory_path() / "c4_runtime" / hex / kObjectFolder;
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
		bool Compile(const fs::path& root, const fs::path& source, const fs::path& object, bool verbose)
		{
			const std::string clang = ClangPath();
			if (clang.empty()) return false;
			const fs::path partial = object.string() + "." + std::to_string(_getpid()) + ".tmp";
			const std::vector<std::string> args = RuntimeCompileCommand(clang, root.string(), source.string(), partial.string());
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

	std::vector<std::string> RuntimeCompileCommand(const std::string& clang, const std::string& runtimeRoot,
		const std::string& source, const std::string& output)
	{
		std::vector<std::string> args = { clang, "-c", "-target", "x86_64-pc-windows-msvc", kOptimization, "-mstackrealign",
			"-Werror", "-D_CRT_SECURE_NO_WARNINGS", "-I", runtimeRoot };
		if (fs::path(source).extension() == ".cpp") args.push_back("-std=c++20");
		args.push_back(source);
		args.push_back("-o");
		args.push_back(output);
		return args;
	}

	bool EnsureRuntimeObjects(bool verbose, Core::Vector<Core::String>& objects)
	{
		const fs::path root = Toolchain::Runtime();
		if (root.empty())
		{
			std::cerr << "error: the C4 runtime sources were not found: C4 looks in <C4 home>/C4Runtime (set C4_HOME to the "
				"folder that holds C4.exe, Bedrock/, C4Runtime/ and toolchain/)" << std::endl;
			return false;
		}
		const fs::path objectDir = ObjectFolder(root);
		CreateFolders(PathText(objectDir));
		const uint64_t headersKey = HeadersKey(root);
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
			const fs::path object = objectDir / RuntimeObjectName(relative, RuntimeContentKey(Bytes(source), headersKey));
			if (!FileExists(PathText(object))) missing.push_back({ source, object, ObjectStem(relative) });
			objects.Push(Core::String(object.string().c_str()));
		}
		// A toolchain's first build compiles every source: one clang each, at once.
		std::vector<char> failed(missing.size(), 0);
		ParallelFor(missing.size(), std::max(1u, std::thread::hardware_concurrency()), [&](size_t i) {
			if (!Compile(root, missing[i].source, missing[i].object, verbose)) failed[i] = 1;
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
