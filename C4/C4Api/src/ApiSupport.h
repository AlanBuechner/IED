#pragma once
// What the C API's implementation files share.

#include "Macro/PredefinedMacros.h"
#include <cstdint>
#include <map>
#include <string>

namespace C4Api
{
	// The macros NAME or NAME=VALUE name, as after C4.exe -D.
	inline std::map<std::string, std::string> DefineMap(const char* const* defines, uint32_t count)
	{
		std::map<std::string, std::string> result;
		for (uint32_t i = 0; i < count; ++i)
			if (defines && defines[i])
			{
				const C4Compiler::MacroDefine define = C4Compiler::ParseMacroDefine(defines[i]);
				result[define.name] = define.value;
			}
		return result;
	}
}
