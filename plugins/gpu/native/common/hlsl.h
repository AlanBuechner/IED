// hlsl.h - HLSL compiled with what Windows and the SDKs have (docs/22 "Edit and Replay"): Shader Model 5.x to DXBC
// through Windows' own d3dcompiler_47.dll, 6.x to DXIL through the Windows SDK's dxcompiler.dll, and to SPIR-V through a
// dxcompiler.dll with SPIR-V code generation (the Vulkan SDK's). Each is loaded when first needed; none is a build
// dependency.
#pragma once

#include "fb_base.h"

namespace fb
{
	enum class HlslTarget { Dxbc, Dxil, Spirv };

	// Compiles `entry` of `source` for `profile` ("ps_5_1", "vs_6_0"); `log` gets the compiler's messages, warnings too.
	// False when it did not compile, or when no compiler for the target is there (`log` says so).
	bool CompileHlsl(const std::string& source, const std::string& entry, const std::string& profile, HlslTarget target, std::vector<u8>& code, std::string& log);

	// The compilers' modules (HMODULEs), null when not there: d3dcompiler_47.dll; the Windows SDK's newest
	// dxcompiler.dll (FLASHBANG_DXCOMPILER names another); one with SPIR-V code generation (FLASHBANG_SPIRV_DXCOMPILER
	// names it, else the Vulkan SDK's, by VULKAN_SDK or the newest under C:\VulkanSDK).
	void* Compiler47();
	void* DxCompiler();
	void* SpirvDxCompiler();
}
