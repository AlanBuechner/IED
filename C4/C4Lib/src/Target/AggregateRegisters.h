#pragma once
#include "Target/TargetAbi.h"

namespace C4Compiler
{
	/// Windows x64: an aggregate of exactly 1, 2, 4 or 8 bytes crosses a call as one integer of its width; any other
	/// size in memory (null).
	llvm::Type* Win64AggregateRegisters(const AggregateShape& aggregate);

	/// System V x86-64 (the psABI, section 3.2.3): an aggregate of up to 16 bytes, an eightbyte at a time - one holding
	/// only floats is SSE (`float`, `<2 x float>` or `double`), any other INTEGER (an integer of the bytes it holds); the
	/// one type, or a struct of the two. Null (memory) past 16 bytes, for an unaligned field, an x87 or 128-bit float.
	llvm::Type* SystemVAggregateRegisters(const AggregateShape& aggregate);
} // namespace C4Compiler
