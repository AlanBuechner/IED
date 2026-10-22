#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/NormalizedConversion.h"
#include "Model/Types/SwizzleType.h"
#include "Backend/CodeGen/IntegerPower.h"
#include "Backend/CodeGen/CallEmitter.h"
#include "Backend/CodeGen/ObjectEmitter.h"
#include "Backend/CodeGen/FuncEmitter.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/TypeHandle.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h> // CreateVAStart / CreateVAEnd free functions

namespace Scripting
{
	namespace
	{
		// A packed vector's arithmetic and conversions are its lanes' (through aliases; a reference stays an address).
		const Type* LaneType(const Type* t)
		{
			const Type* named = t;
			while (named && named->GetKind() == TypeKind::Alias) named = static_cast<const AliasType*>(named)->GetAliasedType();
			return named && named->GetKind() == TypeKind::Swizzle ? static_cast<const SwizzleType*>(named)->GetElementType() : t;
		}

		bool IsUnsignedInt(const Type* t)
		{
			t = LaneType(t);
			return t && t->IsUnsignedInteger();
		}

		bool IsPointerLike(const Type* t)
		{
			if (!t) return false;
			const Type* u = TypeHandle::UnwrapToValue(t);
			TypeKind k = u ? u->GetKind() : TypeKind::Invalid;
			return k == TypeKind::Pointer || k == TypeKind::Reference ||
				k == TypeKind::FunPtr || k == TypeKind::Function;
		}

		uint32_t BitWidth(const Type* t) { return static_cast<uint32_t>(t ? t->GetSize() * 8 : 0); }

		// A count of `value`'s bits as `result` (an int32 for an integer, the vector itself for a vector).
		llvm::Value* CountBits(llvm::IRBuilder<>& B, llvm::Intrinsic::ID id, llvm::Value* value, llvm::Type* result)
		{
			llvm::Value* count = id == llvm::Intrinsic::ctpop ? B.CreateUnaryIntrinsic(id, value)
				: B.CreateBinaryIntrinsic(id, value, B.getFalse()); // zero gives the width, not poison
			return count->getType() == result ? count : B.CreateZExtOrTrunc(count, result);
		}

		// A single byte has nothing to swap.
		llvm::Value* SwapBytes(llvm::IRBuilder<>& B, llvm::Value* value)
		{
			return value->getType()->getScalarSizeInBits() < 16 ? value : B.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, value);
		}

		// The upper half of the double-width product.
		llvm::Value* MultiplyHigh(llvm::IRBuilder<>& B, llvm::Value* lhs, llvm::Value* rhs, bool isUnsigned)
		{
			llvm::Type* narrow = lhs->getType();
			const unsigned width = narrow->getScalarSizeInBits();
			llvm::Type* wide = narrow->getWithNewBitWidth(width * 2);
			llvm::Value* a = isUnsigned ? B.CreateZExt(lhs, wide) : B.CreateSExt(lhs, wide);
			llvm::Value* b = isUnsigned ? B.CreateZExt(rhs, wide) : B.CreateSExt(rhs, wide);
			return B.CreateTrunc(B.CreateLShr(B.CreateMul(a, b), llvm::ConstantInt::get(wide, width)), narrow);
		}
	}

	llvm::Value* ExprEmitter::EmitIntrinsicOp(const IntrinsicOpNode* n)
	{
		auto& B = m_Ctx.builder();
		auto& ctx = m_Ctx.mctx().context();
		const Type* t = LaneType(n->type);
		bool isFloat = t && t->IsFloatingPoint();
		bool unsignedInt = IsUnsignedInt(t);

		llvm::Value* lhs = Emit(n->lhs);
		if (!n->rhs) // unary
		{
			switch (n->op)
			{
			case IntrinsicOp::Negate:  return isFloat ? B.CreateFNeg(lhs) : B.CreateNeg(lhs);
			case IntrinsicOp::BitNot:  return B.CreateNot(lhs);
			case IntrinsicOp::CountOnes: return CountBits(B, llvm::Intrinsic::ctpop, lhs, m_Ctx.mctx().Lower(n->type));
			case IntrinsicOp::LeadingZeros: return CountBits(B, llvm::Intrinsic::ctlz, lhs, m_Ctx.mctx().Lower(n->type));
			case IntrinsicOp::TrailingZeros: return CountBits(B, llvm::Intrinsic::cttz, lhs, m_Ctx.mctx().Lower(n->type));
			case IntrinsicOp::ByteSwap: return SwapBytes(B, lhs);
			case IntrinsicOp::Likely: return B.CreateIntrinsic(llvm::Intrinsic::expect, { lhs->getType() }, { lhs, B.getTrue() });
			case IntrinsicOp::Unlikely: return B.CreateIntrinsic(llvm::Intrinsic::expect, { lhs->getType() }, { lhs, B.getFalse() });
			// A read (0), kept in every cache level (3), of data (1).
			case IntrinsicOp::Prefetch:
				return B.CreateIntrinsic(llvm::Intrinsic::prefetch, { lhs->getType() }, { lhs, B.getInt32(0), B.getInt32(3), B.getInt32(1) });
			// Logical NOT yields a boolean (i1): !x is true exactly when x IS zero, compared at the operand's own
			// width. A pointer's zero is the null pointer: `!p` is `p == null`; a func's is a null code pointer
			// (BUG-275: `s && f` negates its right operand twice and compared the func's { code, environment }).
			case IntrinsicOp::BoolNot:
				if (lhs->getType()->isStructTy()) return B.CreateNot(IRContext::AsBool(B, lhs));
				if (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())
					return B.CreateFCmpOEQ(lhs, llvm::ConstantFP::get(lhs->getType(), 0.0));
				if (lhs->getType()->isPointerTy())
					return B.CreateICmpEQ(lhs, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(lhs->getType())));
				return B.CreateICmpEQ(lhs, llvm::ConstantInt::get(lhs->getType(), 0));
			default:                   return lhs;
			}
		}

		llvm::Value* rhs = Emit(n->rhs);
		// SIMD vector op scalar: the lowerer cannot convert this (a swizzle sub-vector is not "numeric"
		// for usual-arithmetic rules), but LLVM needs both operands to be <N x T>. Broadcast the scalar
		// into a vector of the other side's element type - `v.xyz * 2.0f`, `f.xyz *= 3` (int -> float).
		auto splatTo = [&](llvm::Value* scalar, llvm::Type* vecTyRaw) -> llvm::Value* {
			auto* vecTy = llvm::cast<llvm::FixedVectorType>(vecTyRaw);
			llvm::Type* elem = vecTy->getElementType();
			if (scalar->getType() != elem)
			{
				bool sInt = scalar->getType()->isIntegerTy(), eInt = elem->isIntegerTy();
				bool sFlt = scalar->getType()->isFloatTy(), eFlt = elem->isFloatTy();
				if (sInt && eInt) // integer width change: i32 lane into an i64 vector (or wider/narrower)
					scalar = (llvm::cast<llvm::IntegerType>(scalar->getType())->getBitWidth() <
						llvm::cast<llvm::IntegerType>(elem)->getBitWidth())
					? B.CreateZExt(scalar, elem) : B.CreateTrunc(scalar, elem);
				else if (eFlt && sInt) scalar = B.CreateSIToFP(scalar, elem); // int scalar into a float vector (f.xyz *= 3)
				else if (sFlt && eInt) scalar = B.CreateFPToSI(scalar, elem);
				else if (sFlt && eFlt) // float width change: f32 lane into an f64 (double) vector
					scalar = (scalar->getType()->getScalarSizeInBits() < elem->getScalarSizeInBits())
					? B.CreateFPExt(scalar, elem) : B.CreateFPTrunc(scalar, elem);
			}
			return B.CreateVectorSplat(vecTy->getNumElements(), scalar); // insertelement + shufflevector: a shuffle takes vectors
			};
		if (lhs->getType()->isVectorTy() && !rhs->getType()->isVectorTy()) rhs = splatTo(rhs, lhs->getType());
		else if (rhs->getType()->isVectorTy() && !lhs->getType()->isVectorTy()) lhs = splatTo(lhs, rhs->getType());
		// result is always bool, but its operands are whatever they were (float or int). Arithmetic results equal
		// their operand type (the lowerer converts both sides to the common type first), so this is correct for all.
		// For a SIMD vector the decision comes from the ELEMENT type: <N x float> is not itself a float type.
		llvm::Type* opTy = lhs->getType()->isVectorTy() ? llvm::cast<llvm::FixedVectorType>(lhs->getType())->getElementType() : lhs->getType();
		bool opFloat = opTy->isFloatingPointTy();
		bool opUnsigned = IsUnsignedInt(n->lhs->type); // signedness of the common type drives sdiv/udiv, ashr/lshr
		switch (n->op)
		{
		case IntrinsicOp::Add:  return opFloat ? B.CreateFAdd(lhs, rhs) : B.CreateAdd(lhs, rhs);
		case IntrinsicOp::Sub:  return opFloat ? B.CreateFSub(lhs, rhs) : B.CreateSub(lhs, rhs);
		case IntrinsicOp::Mul:  return opFloat ? B.CreateFMul(lhs, rhs) : B.CreateMul(lhs, rhs);
		case IntrinsicOp::Div:  return opFloat ? B.CreateFDiv(lhs, rhs) : (opUnsigned ? B.CreateUDiv(lhs, rhs) : B.CreateSDiv(lhs, rhs));
		case IntrinsicOp::Mod:  return opFloat ? B.CreateFRem(lhs, rhs) : (opUnsigned ? B.CreateURem(lhs, rhs) : B.CreateSRem(lhs, rhs));
		case IntrinsicOp::Pow:
			if (opFloat) return B.CreateBinaryIntrinsic(llvm::Intrinsic::pow, lhs, rhs);
			return B.CreateCall(IntegerPowerFunction(m_Ctx.mctx().module(), llvm::cast<llvm::IntegerType>(opTy), !opUnsigned), { lhs, rhs });
		case IntrinsicOp::Eq:   return opFloat ? B.CreateFCmpOEQ(lhs, rhs) : B.CreateICmpEQ(lhs, rhs);
		case IntrinsicOp::Ne:   return opFloat ? B.CreateFCmpUNE(lhs, rhs) : B.CreateICmpNE(lhs, rhs);   // C: NaN != x is true
		case IntrinsicOp::Lt:   return opFloat ? B.CreateFCmpOLT(lhs, rhs) : (opUnsigned ? B.CreateICmpULT(lhs, rhs) : B.CreateICmpSLT(lhs, rhs));
		case IntrinsicOp::Gt:   return opFloat ? B.CreateFCmpOGT(lhs, rhs) : (opUnsigned ? B.CreateICmpUGT(lhs, rhs) : B.CreateICmpSGT(lhs, rhs));
		case IntrinsicOp::Le:   return opFloat ? B.CreateFCmpOLE(lhs, rhs) : (opUnsigned ? B.CreateICmpULE(lhs, rhs) : B.CreateICmpSLE(lhs, rhs));
		case IntrinsicOp::Ge:   return opFloat ? B.CreateFCmpOGE(lhs, rhs) : (opUnsigned ? B.CreateICmpUGE(lhs, rhs) : B.CreateICmpSGE(lhs, rhs));
		case IntrinsicOp::BitAnd:  return B.CreateAnd(lhs, rhs);
		case IntrinsicOp::BitOr:   return B.CreateOr(lhs, rhs);
		case IntrinsicOp::BitXor:  return B.CreateXor(lhs, rhs);
		case IntrinsicOp::Shl:     return B.CreateShl(lhs, rhs);
		case IntrinsicOp::RotateLeft:  return B.CreateIntrinsic(llvm::Intrinsic::fshl, { lhs->getType() }, { lhs, lhs, rhs });
		case IntrinsicOp::RotateRight: return B.CreateIntrinsic(llvm::Intrinsic::fshr, { lhs->getType() }, { lhs, lhs, rhs });
		case IntrinsicOp::MulHigh:     return MultiplyHigh(B, lhs, rhs, opUnsigned);
		case IntrinsicOp::Shr:     return unsignedInt ? B.CreateLShr(lhs, rhs) : B.CreateAShr(lhs, rhs);
		default:                   return lhs;
		}
	}

	llvm::Value* ExprEmitter::EmitConvert(const ConvertNode* n)
	{
		auto& B = m_Ctx.builder();
		llvm::Value* src = Emit(n->src);
		llvm::Type* target = m_Ctx.mctx().Lower(n->to);
		if (n->keepsBits) return B.CreateBitCast(src, target);
		// A packed vector converts lane by lane (D-62): the lanes' types pick the instruction, which works on vectors.
		const Type* from = LaneType(n->from);
		const Type* to = LaneType(n->to);

		if (IsPointerLike(from) && IsPointerLike(to)) return AdjustObjectPointer(src, n->from, n->to);

		bool fromInt = from && from->IsIntegral();
		bool toInt = to && to->IsIntegral();
		uint32_t fb = BitWidth(from), tb = BitWidth(to);

		// Anything becomes a bool by being non-zero (a pointer by being non-null), never by keeping its low bit.
		if (to && to->GetKind() == TypeKind::Bool && from && from->GetKind() != TypeKind::Bool)
		{
			if (IsPointerLike(from)) return B.CreateICmpNE(src, llvm::Constant::getNullValue(src->getType()));
			if (from->IsFloatingPoint()) return B.CreateFCmpUNE(src, llvm::ConstantFP::get(src->getType(), 0.0));
			if (fromInt) return B.CreateICmpNE(src, llvm::ConstantInt::get(src->getType(), 0));
		}

		// Integer <-> pointer: the integer holds/represents an address - a `p == 0` zero lifted to the
		// pointer's type, or a pointer read back as an integer. Under opaque pointers this is NOT a bitcast
		// (an i32->ptr bitcast is invalid IR): it is a width-adjusted IntToPtr/PtrToInt. A sub-pointer-width
		// int zero-extends into the full 64-bit address - `(void*)6` must read back as 0x6, not carry garbage
		// in the high bytes (the Win32 handle idiom depends on it).
		if ((fromInt && IsPointerLike(to)) || (IsPointerLike(from) && toInt))
		{
			llvm::Type* ptrW = llvm::IntegerType::get(m_Ctx.mctx().context(), 64); // pointer width on this target
			if (fromInt && IsPointerLike(to))
			{
				if (fb < 64) src = B.CreateZExt(src, ptrW);
				return B.CreateIntToPtr(src, target);
			}
			llvm::Value* p = B.CreatePtrToInt(src, ptrW);
			if (tb < 64) p = B.CreateTrunc(p, target);
			return p;
		}

		// Every numeric conversion (cast, init, assignment) passes through here, so this is the one place a
		// normalized value is encoded and decoded (D-38).
		if ((from && from->IsNormalized()) || (to && to->IsNormalized()))
			if (llvm::Value* normalized = NormalizedConversion::Emit(B, src, from, to)) return normalized;
		if (fromInt && toInt)
		{
			// The widths of the values, not of their storage: a bool is an i1 stored in a byte. Widening: a bool is
			// 0/1 and must zero-extend (a sign-extend of i1 true yields -1, not 1).
			const uint32_t srcBits = src->getType()->getScalarSizeInBits();
			const uint32_t dstBits = target->getScalarSizeInBits();
			bool zext = (from->GetKind() == TypeKind::Bool) || IsUnsignedInt(from);
			if (dstBits > srcBits) return zext ? B.CreateZExt(src, target) : B.CreateSExt(src, target);
			if (dstBits < srcBits) return B.CreateTrunc(src, target);
			return src; // same width
		}
		bool fromFloat = from && from->IsFloatingPoint();
		bool toFloat = to && to->IsFloatingPoint();
		// A bool is 0 or 1: as a signed one-bit integer its true would be -1 (BUG-253).
		if (fromInt && toFloat)
			return IsUnsignedInt(from) || from->GetKind() == TypeKind::Bool ? B.CreateUIToFP(src, target) : B.CreateSIToFP(src, target);
		if (fromFloat && toInt) return IsUnsignedInt(to) ? B.CreateFPToUI(src, target) : B.CreateFPToSI(src, target);
		if (fromFloat && toFloat)
		{
			if (tb > fb) return B.CreateFPExt(src, target);
			if (tb < fb) return B.CreateFPTrunc(src, target);
			return src;
		}

		// Fallback: a same-size reinterpretation (e.g. int<->bit pattern). Different sizes that are not the
		// cases above are a lowering contract violation and should not reach codegen.
		if (fb == tb) return B.CreateBitCast(src, target);
		return src;
	}

	llvm::Value* ExprEmitter::AdjustObjectPointer(llvm::Value* src, const Type* from, const Type* to)
	{
		// Between pointers to related object types the address moves to the target sub-object (an upcast
		// adds the base offset, a downcast subtracts it); null stays null. Other pointer conversions keep the
		// bits (opaque pointers).
		auto pointee = [](const Type* t) -> const ObjectType* {
			const Type* p = TypeHandle::UnwrapToValue(t);
			if (!p || p->GetKind() != TypeKind::Pointer) return nullptr;
			const Type* o = TypeHandle::UnwrapToValue(static_cast<const PointerType*>(p)->GetPointeeType());
			return o && o->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(o) : nullptr;
		};
		int64 delta = ClassHierarchy::PointerDelta(pointee(from), pointee(to));
		if (delta == 0) return src;
		auto& B = m_Ctx.builder();
		llvm::Value* moved = B.CreateGEP(llvm::Type::getInt8Ty(m_Ctx.mctx().context()), src,
			llvm::ConstantInt::get(llvm::Type::getInt64Ty(m_Ctx.mctx().context()), delta));
		llvm::Value* isNull = B.CreateICmpEQ(src, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(src->getType())));
		return B.CreateSelect(isNull, src, moved);
	}
} // namespace Scripting
