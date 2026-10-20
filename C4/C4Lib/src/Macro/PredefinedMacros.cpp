#include "Macro/PredefinedMacros.h"
#include "Compiler/Version.h"

namespace Scripting
{
	MacroDefine ParseMacroDefine(const std::string& text)
	{
		const size_t equals = text.find('=');
		if (equals == std::string::npos) return { text, "1" };
		return { text.substr(0, equals), text.substr(equals + 1) };
	}

	std::map<std::string, std::string> PredefinedMacros(bool test, const CpuTarget& target, const std::vector<MacroDefine>& defines)
	{
		std::map<std::string, std::string> macros = target.Macros();
		macros["PLATFORM_WINDOWS"] = "1";
		macros["ARCH_X64"] = "1";
		macros["C4_VERSION"] = std::to_string(CompilerVersion::Encoded);
		if (test) macros["TEST"] = "1";
		for (const MacroDefine& define : defines)
			macros[define.name] = define.value;
		return macros;
	}
}
