#include "Compiler/RuntimeObjects.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/Toolchain.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <functional>
#include <algorithm>
#include <iostream>
#include <system_error>
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

		std::vector<fs::path> CollectSources(const fs::path& root)
		{
			std::vector<fs::path> sources;
			for (const auto& entry : fs::recursive_directory_iterator(root))
			{
				const fs::path ext = entry.path().extension();
				if (ext == ".c" || ext == ".cpp") sources.push_back(entry.path());
			}
			std::sort(sources.begin(), sources.end());
			return sources;
		}

		std::string Bytes(const fs::path& file)
		{
			std::ifstream in(file, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		// Every header's path and bytes, in path order: a changed header changes every source's key.
		uint64_t HeadersKey(const fs::path& root)
		{
			std::vector<fs::path> headers;
			for (const auto& entry : fs::recursive_directory_iterator(root))
				if (entry.path().extension() == ".h") headers.push_back(entry.path());
			std::sort(headers.begin(), headers.end());
			uint64_t key = kContentSeed;
			for (const fs::path& header : headers)
				key = RuntimeContentKey(Bytes(header), RuntimeContentKey(fs::relative(header, root).generic_string(), key));
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
			for (const auto& entry : fs::directory_iterator(objectDir, ec))
			{
				const std::string name = entry.path().filename().string();
				const bool sameSource = name.rfind(stem + "-", 0) == 0 && entry.path().extension() == ".obj";
				if (sameSource && entry.path() != current) fs::remove(entry.path(), ec); // another build may still hold it
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
				std::cout << "Compiling C4 runtime:";
				for (const std::string& a : args) std::cout << " " << a;
				std::cout << std::endl;
			}
			std::error_code ec;
			if (!RunTool(args))
			{
				fs::remove(partial, ec);
				return false;
			}
			fs::rename(partial, object, ec);
			if (!ec) return true;
			fs::remove(partial, ec);
			return fs::exists(object, ec); // another process put its copy in place first
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
		std::error_code ec;
		fs::create_directories(objectDir, ec);
		const uint64_t headersKey = HeadersKey(root);
		for (const fs::path& source : CollectSources(root))
		{
			const std::string relative = fs::relative(source, root).generic_string();
			const fs::path object = objectDir / RuntimeObjectName(relative, RuntimeContentKey(Bytes(source), headersKey));
			if (!fs::exists(object, ec))
			{
				if (!Compile(root, source, object, verbose))
				{
					std::cerr << "error: failed to compile the C4 runtime source " << source.string() << std::endl;
					return false;
				}
				RemoveOtherVersions(objectDir, ObjectStem(relative), object);
			}
			objects.Push(Core::String(object.string().c_str()));
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
