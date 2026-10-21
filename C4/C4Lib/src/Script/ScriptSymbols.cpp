#include "Script/ScriptSymbols.h"
#include <llvm/Support/DynamicLibrary.h>
#include <cmath>
#include <cstring>
#include <unordered_map>

extern "C" void __chkstk();
extern "C" int _fltused;

namespace Scripting
{
	void* SupportSymbol(const std::string& name)
	{
		static const std::unordered_map<std::string, void*> kSymbols = {
			{ "memcpy", (void*)&memcpy }, { "memmove", (void*)&memmove }, { "memset", (void*)&memset }, { "memcmp", (void*)&memcmp },
			{ "__chkstk", (void*)&__chkstk }, { kFloatingPointMarker, (void*)&_fltused },
			{ "sin", (void*)(double (*)(double))&sin }, { "cos", (void*)(double (*)(double))&cos }, { "tan", (void*)(double (*)(double))&tan },
			{ "asin", (void*)(double (*)(double))&asin }, { "acos", (void*)(double (*)(double))&acos }, { "atan", (void*)(double (*)(double))&atan },
			{ "atan2", (void*)(double (*)(double, double))&atan2 }, { "exp", (void*)(double (*)(double))&exp }, { "exp2", (void*)(double (*)(double))&exp2 },
			{ "log", (void*)(double (*)(double))&log }, { "log2", (void*)(double (*)(double))&log2 }, { "log10", (void*)(double (*)(double))&log10 },
			{ "pow", (void*)(double (*)(double, double))&pow }, { "fmod", (void*)(double (*)(double, double))&fmod }, { "sqrt", (void*)(double (*)(double))&sqrt },
			{ "floor", (void*)(double (*)(double))&floor }, { "ceil", (void*)(double (*)(double))&ceil }, { "round", (void*)(double (*)(double))&round },
			{ "trunc", (void*)(double (*)(double))&trunc }, { "rint", (void*)(double (*)(double))&rint }, { "nearbyint", (void*)(double (*)(double))&nearbyint },
			{ "fma", (void*)(double (*)(double, double, double))&fma }, { "ldexp", (void*)(double (*)(double, int))&ldexp },
			{ "sinf", (void*)(float (*)(float))&sinf }, { "cosf", (void*)(float (*)(float))&cosf }, { "tanf", (void*)(float (*)(float))&tanf },
			{ "asinf", (void*)(float (*)(float))&asinf }, { "acosf", (void*)(float (*)(float))&acosf }, { "atanf", (void*)(float (*)(float))&atanf },
			{ "atan2f", (void*)(float (*)(float, float))&atan2f }, { "expf", (void*)(float (*)(float))&expf }, { "exp2f", (void*)(float (*)(float))&exp2f },
			{ "logf", (void*)(float (*)(float))&logf }, { "log2f", (void*)(float (*)(float))&log2f }, { "log10f", (void*)(float (*)(float))&log10f },
			{ "powf", (void*)(float (*)(float, float))&powf }, { "fmodf", (void*)(float (*)(float, float))&fmodf }, { "sqrtf", (void*)(float (*)(float))&sqrtf },
			{ "floorf", (void*)(float (*)(float))&floorf }, { "ceilf", (void*)(float (*)(float))&ceilf }, { "roundf", (void*)(float (*)(float))&roundf },
			{ "truncf", (void*)(float (*)(float))&truncf }, { "rintf", (void*)(float (*)(float))&rintf }, { "nearbyintf", (void*)(float (*)(float))&nearbyintf },
			{ "fmaf", (void*)(float (*)(float, float, float))&fmaf }, { "ldexpf", (void*)(float (*)(float, int))&ldexpf },
		};
		auto it = kSymbols.find(name);
		return it == kSymbols.end() ? nullptr : it->second;
	}

	void* ResolveScriptSymbol(const HostSymbolTable& host, const std::string& name)
	{
		if (name == "__c4_host_CxxFrameHandler3") return host.Find("__CxxFrameHandler3");
		if (void* symbol = host.Find(name)) return symbol;
		if (void* support = SupportSymbol(name)) return support;
		return llvm::sys::DynamicLibrary::getPermanentLibrary(nullptr).getAddressOfSymbol(name.c_str());
	}
} // namespace Scripting
