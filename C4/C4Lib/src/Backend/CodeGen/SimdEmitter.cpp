#include "Backend/CodeGen/ExprEmitter.h"
#include "Backend/CodeGen/ModuleContext.h"
#include "Model/Types/SwizzleType.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicsX86.h>
#include <llvm/Support/MathExtras.h>
#include <functional>
#include <vector>

namespace Scripting
{
	namespace
	{
		using Op = SimdNode::Op;
		using Builder = llvm::IRBuilderBase;
		using Combine = std::function<llvm::Value*(llvm::Value*, llvm::Value*)>;

		const Type* ElementOf(const Type* t)
		{
			const auto* lanes = dynamic_cast<const SwizzleType*>(t);
			return lanes ? lanes->GetElementType() : t;
		}

		unsigned LaneCount(llvm::Value* vector) { return llvm::cast<llvm::FixedVectorType>(vector->getType())->getNumElements(); }

		llvm::SmallVector<int, 16> Range(unsigned first, unsigned count)
		{
			llvm::SmallVector<int, 16> lanes;
			for (unsigned i = 0; i < count; ++i) lanes.push_back(static_cast<int>(first + i));
			return lanes;
		}

		// The SSE rule (D-62): `a < b ? a : b` per lane, so a NaN in either lane gives b.
		llvm::Value* Extreme(Builder& B, llvm::Value* a, llvm::Value* b, const Type* element, bool maximum)
		{
			if (element->IsFloatingPoint()) return B.CreateSelect(maximum ? B.CreateFCmpOGT(a, b) : B.CreateFCmpOLT(a, b), a, b);
			const bool isUnsigned = element->IsUnsignedInteger();
			const llvm::Intrinsic::ID id = maximum ? (isUnsigned ? llvm::Intrinsic::umax : llvm::Intrinsic::smax)
				: (isUnsigned ? llvm::Intrinsic::umin : llvm::Intrinsic::smin);
			return B.CreateBinaryIntrinsic(id, a, b);
		}

		llvm::Value* Absolute(Builder& B, llvm::Value* v, const Type* element)
		{
			if (element->IsFloatingPoint()) return B.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, v);
			if (element->IsUnsignedInteger()) return v;
			return B.CreateBinaryIntrinsic(llvm::Intrinsic::abs, v, B.getFalse()); // the most negative value stays itself
		}

		llvm::Intrinsic::ID Rounding(Op op)
		{
			switch (op)
			{
			case Op::Sqrt: return llvm::Intrinsic::sqrt;
			case Op::Floor: return llvm::Intrinsic::floor;
			case Op::Ceil: return llvm::Intrinsic::ceil;
			case Op::Round: return llvm::Intrinsic::round;
			default: return llvm::Intrinsic::trunc;
			}
		}

		// Bit i is mask lane i.
		llvm::Value* Bitmask(Builder& B, llvm::Value* mask)
		{
			llvm::Value* bits = B.CreateBitCast(mask, B.getIntNTy(LaneCount(mask)));
			return B.CreateZExt(bits, B.getInt32Ty());
		}

		// The lanes padded to a power of two: with `pad` (an identity of the reduction), or with lane 0.
		llvm::Value* PaddedToPowerOfTwo(Builder& B, llvm::Value* v, llvm::Constant* pad)
		{
			const unsigned lanes = LaneCount(v);
			const unsigned width = static_cast<unsigned>(llvm::PowerOf2Ceil(lanes));
			if (width == lanes) return v;
			llvm::SmallVector<int, 16> picks = Range(0, lanes);
			for (unsigned i = lanes; i < width; ++i) picks.push_back(pad ? static_cast<int>(lanes) : 0);
			return pad ? B.CreateShuffleVector(v, llvm::ConstantVector::getSplat(llvm::ElementCount::getFixed(lanes), pad), picks)
				: B.CreateShuffleVector(v, picks);
		}

		// A float reduction in a fixed order (D-62): the upper half of the lanes combines into the lower half until one
		// lane is left, so the result does not depend on the optimizer or the target.
		llvm::Value* HalvingTree(Builder& B, llvm::Value* v, llvm::Constant* pad, const Combine& combine)
		{
			v = PaddedToPowerOfTwo(B, v, pad);
			for (unsigned width = LaneCount(v) / 2; width >= 1; width /= 2)
				v = combine(B.CreateShuffleVector(v, Range(0, width)), B.CreateShuffleVector(v, Range(width, width)));
			return B.CreateExtractElement(v, static_cast<uint64_t>(0));
		}

		llvm::Value* FloatReduction(Builder& B, llvm::Value* v, Op op, const Type* element)
		{
			llvm::Type* scalar = llvm::cast<llvm::FixedVectorType>(v->getType())->getElementType();
			switch (op)
			{
			case Op::ReduceAdd:
				return HalvingTree(B, v, llvm::ConstantFP::getNegativeZero(scalar), [&](llvm::Value* a, llvm::Value* b) { return B.CreateFAdd(a, b); });
			case Op::ReduceMul:
				return HalvingTree(B, v, llvm::ConstantFP::get(scalar, 1.0), [&](llvm::Value* a, llvm::Value* b) { return B.CreateFMul(a, b); });
			default:
				return HalvingTree(B, v, nullptr, [&](llvm::Value* a, llvm::Value* b) { return Extreme(B, a, b, element, op == Op::ReduceMax); });
			}
		}

		llvm::Value* IntegerReduction(Builder& B, llvm::Value* v, Op op, const Type* element)
		{
			const bool isSigned = !element->IsUnsignedInteger();
			switch (op)
			{
			case Op::ReduceAdd: return B.CreateAddReduce(v);
			case Op::ReduceMul: return B.CreateMulReduce(v);
			case Op::ReduceMin: return B.CreateIntMinReduce(v, isSigned);
			case Op::ReduceMax: return B.CreateIntMaxReduce(v, isSigned);
			case Op::ReduceAnd: return B.CreateAndReduce(v);
			case Op::ReduceOr: return B.CreateOrReduce(v);
			default: return B.CreateXorReduce(v);
			}
		}

		// pclmulqdq at the vector's width; the immediate's bit 0 picks a's 64-bit lane, bit 4 b's.
		llvm::Value* CarrylessProduct(Builder& B, llvm::Value* a, llvm::Value* b, int32 i, int32 j)
		{
			const unsigned lanes = LaneCount(a);
			const llvm::Intrinsic::ID id = lanes == 2 ? llvm::Intrinsic::x86_pclmulqdq
				: lanes == 4 ? llvm::Intrinsic::x86_pclmulqdq_256 : llvm::Intrinsic::x86_pclmulqdq_512;
			return B.CreateIntrinsic(id, {}, { a, b, B.getInt8((uint8)(i | (j << 4))) });
		}

		llvm::Value* AllLanes(Builder& B, unsigned lanes) { return llvm::ConstantVector::getSplat(llvm::ElementCount::getFixed(lanes), B.getTrue()); }

		// The address of each lane: `base + indices[i]` elements, the indices widened by their signedness.
		llvm::Value* LaneAddresses(Builder& B, llvm::Value* base, llvm::Value* indices, llvm::Type* element, const Type* indexElement)
		{
			llvm::Type* wide = llvm::FixedVectorType::get(B.getInt64Ty(), LaneCount(indices));
			llvm::Value* offsets = indexElement->IsUnsignedInteger() ? B.CreateZExt(indices, wide) : B.CreateSExt(indices, wide);
			return B.CreateGEP(element, base, offsets);
		}
	}

	llvm::Value* ExprEmitter::EmitSimd(const SimdNode* n)
	{
		std::vector<llvm::Value*> v;
		for (const HIRNode* operand : n->operands)
		{
			llvm::Value* value = Emit(operand);
			if (!value) return nullptr;
			v.push_back(value);
		}
		auto& B = m_Ctx.builder();
		const Type* element = ElementOf(n->type);
		switch (n->op)
		{
		case Op::Min: case Op::Max: return Extreme(B, v[0], v[1], element, n->op == Op::Max);
		case Op::Abs: return Absolute(B, v[0], element);
		case Op::Sqrt: case Op::Floor: case Op::Ceil: case Op::Round: case Op::Trunc: return B.CreateUnaryIntrinsic(Rounding(n->op), v[0]);
		case Op::MulAdd: return B.CreateIntrinsic(llvm::Intrinsic::fmuladd, { v[0]->getType() }, { v[0], v[1], v[2] });
		case Op::Fma: return B.CreateIntrinsic(llvm::Intrinsic::fma, { v[0]->getType() }, { v[0], v[1], v[2] });
		case Op::Clmul: return CarrylessProduct(B, v[0], v[1], n->indices[0], n->indices[1]);
		case Op::Select: return B.CreateSelect(v[0], v[1], v[2]);
		case Op::Any: return B.CreateOrReduce(v[0]);
		case Op::All: return B.CreateAndReduce(v[0]);
		case Op::Bitmask: return Bitmask(B, v[0]);
		case Op::Shuffle: return v.size() == 2 ? B.CreateShuffleVector(v[0], v[1], n->indices) : B.CreateShuffleVector(v[0], n->indices);
		case Op::Concat: return B.CreateShuffleVector(v[0], v[1], Range(0, 2 * LaneCount(v[0])));
		case Op::Splat: return B.CreateVectorSplat(llvm::cast<llvm::FixedVectorType>(m_Ctx.mctx().Lower(n->type))->getNumElements(), v[0]);
		default: break;
		}
		return EmitSimdMemoryOrReduction(n, v);
	}

	llvm::Value* ExprEmitter::EmitSimdMemoryOrReduction(const SimdNode* n, const std::vector<llvm::Value*>& v)
	{
		auto& B = m_Ctx.builder();
		ModuleContext& module = m_Ctx.mctx();
		const Type* element = ElementOf(n->type);
		llvm::MDNode* lanes = module.aliasTags().OfType(n->op == Op::Store || n->op == Op::StoreMasked ? n->operands[1]->type : n->type);
		switch (n->op)
		{
		case Op::Load:
			return Attach(B.CreateAlignedLoad(module.Lower(n->type), v[0], llvm::Align(element->GetSize())), lanes);
		case Op::LoadMasked:
			return Attach(B.CreateMaskedLoad(module.Lower(n->type), v[0], llvm::Align(element->GetSize()), v[1], llvm::Constant::getNullValue(module.Lower(n->type))), lanes);
		case Op::Store: case Op::StoreMasked:
		{
			const llvm::Align align(ElementOf(n->operands[1]->type)->GetSize());
			if (n->op == Op::Store) Attach(B.CreateAlignedStore(v[1], v[0], align), lanes);
			else Attach(B.CreateMaskedStore(v[1], v[0], align, v[2]), lanes);
			return nullptr;
		}
		case Op::Gather: case Op::GatherMasked:
		{
			llvm::Type* vectorType = module.Lower(n->type);
			llvm::Value* addresses = LaneAddresses(B, v[0], v[1], module.Lower(element), ElementOf(n->operands[1]->type));
			llvm::Value* mask = n->op == Op::Gather ? AllLanes(B, LaneCount(v[1])) : v[2];
			return Attach(B.CreateMaskedGather(vectorType, addresses, llvm::Align(element->GetSize()), mask, llvm::Constant::getNullValue(vectorType)), lanes);
		}
		default:
			return element->IsFloatingPoint() ? FloatReduction(B, v[0], n->op, element) : IntegerReduction(B, v[0], n->op, element);
		}
	}
} // namespace Scripting
