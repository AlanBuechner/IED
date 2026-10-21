#pragma once
#include "Backend/HIR/HIRNodes.h"
#include "Model/ConstantValue.h"
#include <map>
#include <optional>
#include <vector>

namespace Scripting
{
	struct SymbolInfo;

	/// What a global holds before anything runs: its bytes, and the string literals some of its pointers point at, by
	/// their offset (their bytes are zero).
	struct ConstantData
	{
		std::vector<uint8> bytes;
		std::map<uint64, ConstantValue> strings;
	};

	/// The data a global starts holding when its start only stores constants into it - numbers, null and string
	/// literals - such as a table of Bedrock's (CRC-32, SHA-256, keyword names) written as a braced list: it then starts
	/// as data in the image, not as code that runs before main. Nothing when any step of `start` does more: a call, a
	/// loop, a value that is not such a constant.
	std::optional<ConstantData> ConstantImage(const BlockNode& start, const SymbolInfo& global, uint64 size);
} // namespace Scripting
