#pragma once

namespace llvm { class Module; }

namespace Scripting
{
	/// A packed vector takes exactly N * sizeof(T) bytes, aligned to the largest power of two dividing that size
	/// (SwizzleType). LLVM assumes more for a vector whose size is not a power of two (a <3 x float> is 16-aligned
	/// to it), so every load and store of one is given the alignment C4 guarantees.
	void AlignVectorAccesses(llvm::Module& module);
} // namespace Scripting
