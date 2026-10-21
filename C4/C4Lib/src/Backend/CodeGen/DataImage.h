#pragma once
#include "Backend/HIR/ConstantImage.h"
#include <functional>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>

namespace Scripting
{
	/// The constant a pointer to a string literal is (the literal's private global).
	using StringConstant = std::function<llvm::Constant*(const ConstantValue&)>;

	/// The constant of `type` whose memory is `data` from byte `offset` on (little-endian, laid out as `layout` lays the
	/// type out): a global that starts as data (ConstantImage). Integers, floats, pointers (null, or a string literal
	/// `data` names at their offset) and any arrays, structs and vectors of them; null for another type, or for a
	/// pointer whose bytes are not zero.
	llvm::Constant* DataImage(llvm::Type* type, const ConstantData& data, uint64 offset, const llvm::DataLayout& layout,
		const StringConstant& stringAt);
} // namespace Scripting
