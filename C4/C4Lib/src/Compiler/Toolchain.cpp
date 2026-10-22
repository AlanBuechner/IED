#include "Compiler/Toolchain.h"
#include "Compiler/FileIo.h"
#include <cstdlib>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
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
	}

	fs::path Executable()
	{
#ifdef _WIN32
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH) return fs::path(buffer);
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
		const fs::path installed = Home() / "toolchain" / "bin" / "clang.exe";
		if (IsFile(installed)) return installed;
		return InRepository(fs::path("vendor") / "llvm" / "bin" / "clang.exe");
	}

	fs::path Runtime()
	{
		const fs::path installed = Home() / "C4Runtime";
		if (IsFolder(installed)) return installed;
		return InRepository("C4Runtime");
	}

	fs::path CBind()
	{
		const fs::path installed = Home() / "CBind" / "CBind.exe";
		if (IsFile(installed)) return installed;
		const fs::path sibling = ExecutableFolder().parent_path() / "CBind" / "CBind.exe";
		return IsFile(sibling) ? sibling : fs::path();
	}

	fs::path ScriptHost()
	{
		const fs::path installed = Home() / "C4Script";
		if (IsFile(installed / "C4Script.dll")) return installed;
		const fs::path sibling = ExecutableFolder().parent_path() / "C4Script";
		return IsFile(sibling / "C4Script.dll") ? sibling : fs::path();
	}

	fs::path Bedrock()
	{
		const fs::path installed = Home() / "Bedrock";
		if (IsFolder(installed)) return installed;
		const fs::path repository = InRepository("Bedrock");
		return repository.empty() ? installed : repository;
	}
}
