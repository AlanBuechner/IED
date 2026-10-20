#include "Backend/CodeGen/NormalizedConversion.h"
#include "Model/TypeHandle.h"
#include "Model/Types/Type.h"
#include <cmath>

namespace Scripting::NormalizedConversion
{
	namespace
	{
		// The stored range of a normalized type: snormN [-(2^(N-1)-1), 2^(N-1)-1], unormN [0, 2^N-1].
		struct Range
		{
			int64 lowest = 0;
			int64 highest = 0;
			double scale = 0.0;
			bool isUnsigned = false;
			uint32 bits = 0;
		};

		Range RangeOf(const Type* t)
		{
			const uint32 bits = static_cast<uint32>(t->GetSize() * 8);
			if (t->IsUnsignedNormalized())
			{
				const uint64 max = bits >= 64 ? ~0ull : (1ull << bits) - 1;
				return { 0, static_cast<int64>(max), static_cast<double>(max), true, bits };
			}
			const int64 max = static_cast<int64>((1ull << (bits - 1)) - 1);
			return { -max, max, static_cast<double>(max), false, bits };
		}

		// A normalized value computes in float, in double for 32 and 64 bits.
		llvm::Type* ValueType(llvm::IRBuilder<>& B, const Type* normalized)
		{
			return normalized->GetSize() >= 4 ? B.getDoubleTy() : B.getFloatTy();
		}

		llvm::Type* FloatType(llvm::IRBuilder<>& B, const Type* floating)
		{
			return floating->GetKind() == TypeKind::Float64 ? B.getDoubleTy() : B.getFloatTy();
		}

		llvm::Constant* Encoding(llvm::Type* storage, int64 raw, const Range& range)
		{
			return range.isUnsigned ? llvm::ConstantInt::get(storage, static_cast<uint64>(raw)) : llvm::ConstantInt::getSigned(storage, raw);
		}

		// Clamped to the range, scaled, rounded to nearest (rint: ties to even in the default mode); NaN stores 0.
		llvm::Value* FromFloat(llvm::IRBuilder<>& B, llvm::Value* src, const Type* to)
		{
			const Range range = RangeOf(to);
			llvm::Type* storage = B.getIntNTy(range.bits);
			llvm::Type* work = to->GetSize() >= 4 || src->getType()->isDoubleTy() ? B.getDoubleTy() : B.getFloatTy();
			llvm::Value* x = B.CreateFPCast(src, work);
			llvm::Value* scaled = B.CreateUnaryIntrinsic(llvm::Intrinsic::rint, B.CreateFMul(x, llvm::ConstantFP::get(work, range.scale)));
			llvm::Value* raw = range.isUnsigned ? B.CreateFPToUI(scaled, storage) : B.CreateFPToSI(scaled, storage);
			raw = B.CreateSelect(B.CreateFCmpOLE(x, llvm::ConstantFP::get(work, range.isUnsigned ? 0.0 : -1.0)), Encoding(storage, range.lowest, range), raw);
			raw = B.CreateSelect(B.CreateFCmpOGE(x, llvm::ConstantFP::get(work, 1.0)), Encoding(storage, range.highest, range), raw);
			return B.CreateSelect(B.CreateFCmpUNO(x, x), Encoding(storage, 0, range), raw);
		}

		// The stored integer over the scale; a snorm's lowest encoding (-128 for snorm8) reads as -1.
		llvm::Value* ToFloat(llvm::IRBuilder<>& B, llvm::Value* raw, const Type* from, llvm::Type* target)
		{
			const Range range = RangeOf(from);
			llvm::Type* work = ValueType(B, from);
			llvm::Value* value = range.isUnsigned ? B.CreateUIToFP(raw, work) : B.CreateSIToFP(raw, work);
			value = B.CreateFDiv(value, llvm::ConstantFP::get(work, range.scale));
			if (!range.isUnsigned) value = B.CreateMaxNum(value, llvm::ConstantFP::get(work, -1.0));
			return B.CreateFPCast(value, target);
		}

		// An integer is the encoding itself, clamped to the range: `snorm8 s = 200` stores 127.
		llvm::Value* FromInteger(llvm::IRBuilder<>& B, llvm::Value* src, const Type* from, const Type* to)
		{
			const Range range = RangeOf(to);
			llvm::Type* i64 = B.getInt64Ty();
			const bool srcUnsigned = from->IsUnsignedInteger() || from->GetKind() == TypeKind::Bool;
			llvm::Value* wide = srcUnsigned ? B.CreateZExt(src, i64) : B.CreateSExt(src, i64);
			llvm::Value* zero = B.getInt64(0);
			// Every uint64 is a unorm64 encoding; a negative integer stores 0.
			if (range.isUnsigned && range.bits >= 64) return srcUnsigned ? wide : B.CreateSelect(B.CreateICmpSLT(wide, zero), zero, wide);
			// An unsigned source of 64 bits can read as a negative i64 (its value is at least 2^63): that is above the range.
			llvm::Value* highest = llvm::ConstantInt::getSigned(i64, range.highest);
			llvm::Value* lowest = llvm::ConstantInt::getSigned(i64, range.lowest);
			if (srcUnsigned) wide = B.CreateSelect(B.CreateICmpSLT(wide, zero), highest, wide);
			wide = B.CreateSelect(B.CreateICmpSLT(wide, lowest), lowest, wide);
			wide = B.CreateSelect(B.CreateICmpSGT(wide, highest), highest, wide);
			return B.CreateTrunc(wide, B.getIntNTy(range.bits));
		}
	}

	llvm::Value* Emit(llvm::IRBuilder<>& B, llvm::Value* src, const Type* from, const Type* to)
	{
		from = TypeHandle::UnwrapToValue(from);
		to = TypeHandle::UnwrapToValue(to);
		if (!from || !to) return nullptr;
		if (from->IsNormalized() && to->IsNormalized())
			return from->GetKind() == to->GetKind() ? src : FromFloat(B, ToFloat(B, src, from, B.getDoubleTy()), to);
		if (to->IsNormalized() && from->IsFloatingPoint()) return FromFloat(B, src, to);
		if (to->IsNormalized() && from->IsIntegral()) return FromInteger(B, src, from, to);
		if (from->IsNormalized() && to->IsFloatingPoint()) return ToFloat(B, src, from, FloatType(B, to));
		return nullptr;
	}

	int64 RawOf(const ConstantValue& value, const Type* to)
	{
		const Range range = RangeOf(TypeHandle::UnwrapToValue(to));
		if (value.kind != ConstantValue::Kind::Float64)
		{
			const int64 raw = value.AsInt();
			if (range.isUnsigned && range.bits >= 64) return raw; // every uint64 is an encoding
			return raw < range.lowest ? range.lowest : raw > range.highest ? range.highest : raw;
		}
		const double x = value.floatValue;
		if (std::isnan(x)) return 0;
		if (x >= 1.0) return range.highest;
		if (x <= (range.isUnsigned ? 0.0 : -1.0)) return range.lowest;
		const double scaled = std::nearbyint(x * range.scale); // the default rounding mode: ties to even
		return range.isUnsigned ? static_cast<int64>(static_cast<uint64>(scaled)) : static_cast<int64>(scaled);
	}
} // namespace Scripting::NormalizedConversion
