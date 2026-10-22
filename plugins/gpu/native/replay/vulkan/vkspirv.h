// vkspirv.h - SPIR-V read without a dependency: reflection (the entry points, the stage's inputs and outputs by location
// or built-in, its descriptor bindings and push-constant blocks with their members) and a disassembly in spirv-dis's
// style, with friendly names for types and constants.
#pragma once

#include "fb_base.h"

#include <map>

namespace fb::replay::vulkan
{
	struct SpirvInterface
	{
		std::string name;
		i32 location = -1;
		std::string builtin;
		std::string type;
	};

	struct SpirvMember
	{
		std::string name;
		std::string type;
		u32 offset = 0;
		u32 size = 0;
	};

	struct SpirvBinding
	{
		std::string name;
		std::string kind;          // "uniform buffer", "storage buffer", "combined image sampler", "push constants", ...
		u32 set = 0;
		u32 binding = 0;
		u32 count = 1;             // an array's length (0: unbounded)
		u32 size = 0;              // a block's bytes
		std::vector<SpirvMember> members;
	};

	struct SpirvEntry
	{
		u32 model = 0;             // the SPIR-V execution model
		std::string name;
	};

	struct SpirvModule
	{
		u32 version = 0;
		u32 generator = 0;
		u32 bound = 0;
		u32 instructions = 0;
		std::vector<SpirvEntry> entries;
		std::vector<SpirvInterface> inputs;
		std::vector<SpirvInterface> outputs;
		std::vector<SpirvBinding> bindings;
	};

	// Reflection of `count` words; false for something that is not SPIR-V.
	bool ParseSpirv(const u32* words, size_t count, SpirvModule& out);
	std::string DisassembleSpirv(const u32* words, size_t count);
	const char* ExecutionModelName(u32 model);
}
