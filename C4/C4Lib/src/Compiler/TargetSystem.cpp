#include "Compiler/TargetSystem.h"

namespace Scripting
{
	TargetSystem HostSystem()
	{
#if defined(_WIN32)
		return TargetSystem::Windows;
#else
		return TargetSystem::Linux;
#endif
	}

	const char* TargetName(TargetSystem system)
	{
		return system == TargetSystem::Linux ? "linux-x64" : "windows-x64";
	}

	std::optional<TargetSystem> TargetFromName(const std::string& name)
	{
		for (TargetSystem system : { TargetSystem::Windows, TargetSystem::Linux })
			if (name == TargetName(system)) return system;
		return std::nullopt;
	}

	std::string TargetNames()
	{
		return std::string(TargetName(TargetSystem::Windows)) + ", " + TargetName(TargetSystem::Linux);
	}

	const char* TargetTriple(TargetSystem system)
	{
		return system == TargetSystem::Linux ? "x86_64-unknown-linux-gnu" : "x86_64-pc-windows-msvc";
	}

	const char* TargetDataLayout(TargetSystem system)
	{
		// The two differ only in symbol mangling (m:w / m:e).
		return system == TargetSystem::Linux
			? "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
			: "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
	}

	const char* ObjectExtension(TargetSystem system)
	{
		return system == TargetSystem::Linux ? ".o" : ".obj";
	}

	const char* ExecutableExtension(TargetSystem system)
	{
		return system == TargetSystem::Linux ? "" : ".exe";
	}
} // namespace Scripting
