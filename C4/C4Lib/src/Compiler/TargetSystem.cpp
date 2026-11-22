#include "Compiler/TargetSystem.h"

namespace C4Compiler
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
} // namespace C4Compiler
