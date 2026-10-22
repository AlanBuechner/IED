// vkspirv.h - SPIR-V read without a dependency: reflection (the entry points, the stage's inputs and outputs by location
// or built-in, its descriptor bindings and push-constant blocks with their members) and a disassembly in spirv-dis's
// style, with friendly names for types and constants.
#pragma once

#include "bufferlayout.h"
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
		std::vector<BlockMember> block;  // the members placed for the buffer view (a runtime array with 0 elements)
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
	// The listing; `lines` (when given) gets each instruction's line in it, by the instruction's word offset.
	std::string DisassembleSpirv(const u32* words, size_t count, std::map<size_t, u32>* lines = nullptr);
	// Whether opcode `op` has a result id, and whether a result type comes before it.
	bool SpirvResult(u32 op, bool& typed);
	const char* ExecutionModelName(u32 model);
}
