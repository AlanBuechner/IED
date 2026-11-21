#include "Target/AggregateRegisters.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <algorithm>
#include <array>

namespace Scripting
{
	namespace
	{
		constexpr uint64_t kEightbyte = 8;
		constexpr uint64_t kMostInRegisters = 16;

		enum class Class { None, Integer, Sse };
		using Classes = std::array<Class, 2>;

		Class Merge(Class a, Class b)
		{
			if (a == b || b == Class::None) return a;
			if (a == Class::None) return b;
			return Class::Integer; // INTEGER and SSE in one eightbyte: INTEGER
		}

		// Classifies the eightbytes `type` at `offset` touches into `classes`; false when the aggregate goes in memory
		// (an unaligned field, an x87 or 128-bit float).
		bool Classify(const llvm::DataLayout& layout, llvm::Type* type, uint64_t offset, Classes& classes)
		{
			if (offset % layout.getABITypeAlign(type).value() != 0) return false;
			if (auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				const llvm::StructLayout* fields = layout.getStructLayout(structure);
				for (unsigned i = 0; i < structure->getNumElements(); ++i)
					if (!Classify(layout, structure->getElementType(i), offset + fields->getElementOffset(i), classes)) return false;
				return true;
			}
			if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
			{
				const uint64_t stride = layout.getTypeAllocSize(array->getElementType());
				for (uint64_t i = 0; i < array->getNumElements(); ++i)
					if (!Classify(layout, array->getElementType(), offset + i * stride, classes)) return false;
				return true;
			}
			if (type->isX86_FP80Ty() || type->isFP128Ty()) return false;
			const Class own = type->isFloatingPointTy() || type->isVectorTy() ? Class::Sse : Class::Integer;
			const uint64_t end = offset + layout.getTypeStoreSize(type);
			for (uint64_t at = offset; at < end; at += kEightbyte)
				classes[at / kEightbyte] = Merge(classes[at / kEightbyte], own);
			return true;
		}

		// The scalar `type` holds at `offset`, or null when none starts there.
		llvm::Type* ScalarAt(const llvm::DataLayout& layout, llvm::Type* type, uint64_t offset)
		{
			if (auto* structure = llvm::dyn_cast<llvm::StructType>(type))
			{
				const llvm::StructLayout* fields = layout.getStructLayout(structure);
				for (unsigned i = 0; i < structure->getNumElements(); ++i)
				{
					const uint64_t start = fields->getElementOffset(i);
					llvm::Type* field = structure->getElementType(i);
					if (offset >= start && offset < start + layout.getTypeAllocSize(field)) return ScalarAt(layout, field, offset - start);
				}
				return nullptr;
			}
			if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
			{
				const uint64_t stride = layout.getTypeAllocSize(array->getElementType());
				if (stride == 0 || offset / stride >= array->getNumElements()) return nullptr;
				return ScalarAt(layout, array->getElementType(), offset % stride);
			}
			return offset == 0 ? type : nullptr;
		}

		// The register type of the eightbyte at `offset` of an aggregate of `size` bytes: an integer of the bytes it
		// holds, or the float type its floats need (float, <2 x float>, double).
		llvm::Type* EightbyteType(llvm::LLVMContext& ctx, const llvm::DataLayout& layout, llvm::Type* type, Class cls, uint64_t offset, uint64_t size)
		{
			const uint64_t bytes = std::min(kEightbyte, size - offset);
			if (cls != Class::Sse) return llvm::IntegerType::get(ctx, static_cast<unsigned>(bytes * 8));
			llvm::Type* first = ScalarAt(layout, type, offset);
			if (first && first->isFloatTy()) return bytes <= 4 ? first : llvm::FixedVectorType::get(first, 2);
			return llvm::Type::getDoubleTy(ctx);
		}
	}

	llvm::Type* Win64AggregateRegisters(const AggregateShape& aggregate)
	{
		const uint64_t s = aggregate.size;
		if (s == 1 || s == 2 || s == 4 || s == 8) return llvm::IntegerType::get(aggregate.context, static_cast<unsigned>(s * 8)); // one GPR (bytes -> bits)
		return nullptr; // not a single-register size -> hidden pointer
	}

	llvm::Type* SystemVAggregateRegisters(const AggregateShape& aggregate)
	{
		const uint64_t size = aggregate.size;
		if (size == 0 || size > kMostInRegisters) return nullptr;
		llvm::Type* storage = aggregate.storage();
		Classes classes = { Class::None, Class::None };
		if (!storage || !Classify(aggregate.layout, storage, 0, classes)) return nullptr;
		llvm::Type* low = EightbyteType(aggregate.context, aggregate.layout, storage, classes[0], 0, size);
		if (size <= kEightbyte) return low;
		return llvm::StructType::get(aggregate.context, { low, EightbyteType(aggregate.context, aggregate.layout, storage, classes[1], kEightbyte, size) });
	}
} // namespace Scripting
