#include "Target/TargetAbi.h"
#include "Compiler/FileIo.h"
#include "Target/Targets.h"
#include <algorithm>
#include <iostream>

namespace C4Compiler
{
	const TargetAbi& TargetAbi::For(TargetSystem system)
	{
		switch (system)
		{
		case TargetSystem::Linux: return LinuxX64Target();
		case TargetSystem::Windows: break;
		}
		return WindowsX64Target();
	}

	bool NamedFor(const std::string& stem, const char* suffix)
	{
		const size_t length = std::char_traits<char>::length(suffix);
		return stem.size() > length && stem.compare(stem.size() - length, length, suffix) == 0;
	}

	bool WriteLinkInputs(const Core::Vector<Core::String>& objects, const Core::Vector<Core::String>& libraries,
		std::vector<std::string> (*library)(const std::string& argument), const std::string& path)
	{
		std::string text;
		auto add = [&text](std::string argument) {
			std::replace(argument.begin(), argument.end(), '\\', '/');
			text += "\"" + argument + "\"\n";
		};
		for (const Core::String& object : objects) add(object.c_str());
		for (const Core::String& each : libraries)
			for (const std::string& argument : library(each.c_str())) add(argument);
		if (WriteFileBytes(path, text)) return true;
		std::cerr << "error: cannot write the link's inputs to " << path << std::endl;
		return false;
	}
} // namespace C4Compiler
