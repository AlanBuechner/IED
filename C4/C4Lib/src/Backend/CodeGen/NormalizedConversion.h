#pragma once
#include "Model/ConstantValue.h"
#include <llvm/IR/IRBuilder.h>

namespace Scripting
{
	class Type;

	/// Conversions to and from the normalized integers (D-38, docs/language/04 4.1). A snormN stores
	/// round(v * (2^(N-1)-1)) for v in [-1, 1], a unormN round(v * (2^N-1)) for v in [0, 1]; their value
	/// computes in `float` (`double` for 32 and 64 bits). An integer is the stored encoding itself.
	namespace NormalizedConversion
	{
		/// `src` (a `from`) as a `to`, when either is normalized and the value, not the storage, converts:
		/// float <-> normalized and normalized <-> another normalized type. An integer into a normalized type is
		/// its encoding, clamped to the type's range. Null for the conversions that keep the storage
		/// (normalized -> integer, normalized -> bool).
		llvm::Value* Emit(llvm::IRBuilder<>& B, llvm::Value* src, const Type* from, const Type* to);

		/// The encoding a normalized `to` stores for the constant `value`.
		int64 RawOf(const ConstantValue& value, const Type* to);
	}
} // namespace Scripting
