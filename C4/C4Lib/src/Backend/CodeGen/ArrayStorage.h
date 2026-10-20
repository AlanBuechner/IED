#pragma once
#include "Backend/CodeGen/ModuleContext.h"
#include <llvm/IR/IRBuilder.h>
#include <functional>

namespace Scripting
{
	struct FunctionRecord;

	/// The layout of `new T[n]` on the managed heap (docs/compiler/04): when the elements need destruction an
	/// int64 element count comes first, padded to the element's alignment, so the array's finalizer knows how
	/// many elements to destroy; the elements follow.
	namespace ArrayStorage
	{
		/// The bytes before the first element of an array whose elements need destruction.
		uint64 CountBytes(uint64 elementAlignment);

		/// `visit(element address)` for each of `count` (an i64) elements `stride` bytes apart from `first`, in order.
		void ForEach(llvm::IRBuilder<>& B, llvm::Value* first, llvm::Value* count, uint64 stride,
			const std::function<void(llvm::Value*)>& visit);

		/// `void(ptr base)`: reads the element count at `base` and runs `destroy` on every innermost element
		/// (`perElement` of them in each element, `stride` bytes apart), the last first.
		llvm::Function* Finalizer(ModuleContext& m, const FunctionRecord& destroy, uint64 perElement, uint64 stride, uint64 countBytes);
	}
} // namespace Scripting
