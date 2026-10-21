#pragma once

namespace llvm { class Module; }

namespace Scripting
{
	/// Rewrites each copy of a struct or array that the emitter wrote as a load and a store of the whole aggregate into a
	/// memcpy. Unoptimized code generation (FastISel) cannot select an aggregate store and hands the rest of its block to
	/// the slow selector; it selects the memcpy. For -O0 only: the optimizer splits aggregate copies into field copies
	/// that keep their alias tags, which a memcpy loses.
	void LowerAggregateCopies(llvm::Module& module);
}
