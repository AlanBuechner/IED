#pragma once
#include "Compiler/CpuTarget.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm { class Module; }

namespace Scripting
{
	/// How a finished module becomes an object file inside the compiler (roadmap 5.1): LLVM's default optimization
	/// pipeline at the `-O` level, as `clang -O<n>` runs it, then the X86 backend writes the COFF object.
	struct ObjectSettings
	{
		int optLevel = 0;
		const CpuTarget* target = nullptr;
	};

	/// An error the backend reports. `cookie` is the inline-asm location cookie (AsmEmitter::LocationCookie) of the
	/// asm block it is about, or 0.
	struct ObjectDiagnostic
	{
		uint64_t cookie = 0;
		std::string message;
	};

	/// Optimizes `module` and writes its object to `objPath`. False, with what went wrong in `diagnostics`, when the
	/// target, the optimizer or the assembler fails.
	bool EmitObject(llvm::Module& module, const std::string& objPath, const ObjectSettings& settings, std::vector<ObjectDiagnostic>& diagnostics);
}
