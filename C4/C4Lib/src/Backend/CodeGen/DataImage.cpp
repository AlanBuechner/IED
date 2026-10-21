#include "Backend/CodeGen/DataImage.h"
#include <llvm/IR/DerivedTypes.h>
#include <cstring>
#include <vector>

namespace Scripting
{
	namespace
	{
		// The `width` bits at `bytes`, little-endian as the target stores them.
		llvm::APInt BitsAt(const uint8* bytes, unsigned width)
		{
			std::vector<uint64_t> words((width + 63) / 64, 0);
			std::memcpy(words.data(), bytes, (width + 7) / 8);
			return llvm::APInt(width, words);
		}

		bool AllZero(const uint8* bytes, uint64 count)
		{
			for (uint64 i = 0; i < count; ++i)
				if (bytes[i] != 0) return false;
			return true;
		}

		llvm::Constant* PointerAt(llvm::PointerType* type, const ConstantData& data, uint64 offset, const llvm::DataLayout& layout,
			const StringConstant& stringAt)
		{
			if (auto text = data.strings.find(offset); text != data.strings.end()) return stringAt(text->second);
			return AllZero(data.bytes.data() + offset, layout.getTypeAllocSize(type)) ? llvm::ConstantPointerNull::get(type) : nullptr;
		}

		// The `count` elements of `element` from `offset` on, `stride` bytes apart; empty when one has no constant.
		std::vector<llvm::Constant*> Elements(llvm::Type* element, uint64 count, uint64 offset, const ConstantData& data,
			const llvm::DataLayout& layout, const StringConstant& stringAt)
		{
			const uint64 stride = layout.getTypeAllocSize(element);
			std::vector<llvm::Constant*> parts;
			for (uint64 i = 0; i < count; ++i)
			{
				llvm::Constant* part = DataImage(element, data, offset + i * stride, layout, stringAt);
				if (!part) return {};
				parts.push_back(part);
			}
			return parts;
		}

		llvm::Constant* StructAt(llvm::StructType* record, const ConstantData& data, uint64 offset, const llvm::DataLayout& layout,
			const StringConstant& stringAt)
		{
			const llvm::StructLayout* fields = layout.getStructLayout(record);
			std::vector<llvm::Constant*> parts;
			for (unsigned i = 0; i < record->getNumElements(); ++i)
			{
				llvm::Constant* part = DataImage(record->getElementType(i), data, offset + fields->getElementOffset(i), layout, stringAt);
				if (!part) return nullptr;
				parts.push_back(part);
			}
			return llvm::ConstantStruct::get(record, parts);
		}
	}

	llvm::Constant* DataImage(llvm::Type* type, const ConstantData& data, uint64 offset, const llvm::DataLayout& layout,
		const StringConstant& stringAt)
	{
		if (offset + layout.getTypeAllocSize(type) > data.bytes.size()) return nullptr;
		const uint8* bytes = data.bytes.data() + offset;
		if (auto* integer = llvm::dyn_cast<llvm::IntegerType>(type)) return llvm::ConstantInt::get(integer, BitsAt(bytes, integer->getBitWidth()));
		if (type->isFloatTy() || type->isDoubleTy() || type->isHalfTy())
			return llvm::ConstantFP::get(type->getContext(), llvm::APFloat(type->getFltSemantics(), BitsAt(bytes, type->getPrimitiveSizeInBits())));
		if (auto* pointer = llvm::dyn_cast<llvm::PointerType>(type)) return PointerAt(pointer, data, offset, layout, stringAt);
		if (auto* record = llvm::dyn_cast<llvm::StructType>(type)) return StructAt(record, data, offset, layout, stringAt);
		if (auto* array = llvm::dyn_cast<llvm::ArrayType>(type))
		{
			std::vector<llvm::Constant*> parts = Elements(array->getElementType(), array->getNumElements(), offset, data, layout, stringAt);
			return parts.size() == array->getNumElements() ? llvm::ConstantArray::get(array, parts) : nullptr;
		}
		if (auto* vector = llvm::dyn_cast<llvm::FixedVectorType>(type); vector && !vector->getElementType()->isIntegerTy(1)) // i1 lanes pack as bits
		{
			std::vector<llvm::Constant*> parts = Elements(vector->getElementType(), vector->getNumElements(), offset, data, layout, stringAt);
			return parts.size() == vector->getNumElements() ? llvm::ConstantVector::get(parts) : nullptr;
		}
		return nullptr;
	}
} // namespace Scripting
