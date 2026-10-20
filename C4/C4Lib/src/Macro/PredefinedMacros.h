#pragma once
#include "Compiler/CpuTarget.h"
#include <map>
#include <string>
#include <vector>

namespace Scripting
{
	/// A `-D NAME` or `-D NAME=VALUE` definition.
	struct MacroDefine
	{
		std::string name;
		std::string value;
	};

	/// `NAME` or `NAME=VALUE` as written after -D; a NAME without a value is 1.
	MacroDefine ParseMacroDefine(const std::string& text);

	/// The macros every file starts with (docs/language/02 2.5): `TEST` under -test, `PLATFORM_WINDOWS`,
	/// `ARCH_X64`, `C4_VERSION` and the target's `SIMD_*` macros (D-62), then the -D definitions, which may replace them.
	std::map<std::string, std::string> PredefinedMacros(bool test, const CpuTarget& target, const std::vector<MacroDefine>& defines);
}
