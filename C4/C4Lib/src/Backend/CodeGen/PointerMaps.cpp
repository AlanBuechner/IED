#include "Backend/CodeGen/PointerMaps.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

namespace C4Compiler
{
	namespace
	{
		constexpr uint64 kWordBytes = 8;

		const Type* WithoutAliases(const Type* type)
		{
			while (type && type->GetKind() == TypeKind::Alias) type = static_cast<const AliasType*>(type)->GetAliasedType();
			return type;
		}

		// Sets the bits of the words a value's parts may hold a pointer in; Add answers false for a part whose layout it
		// cannot see, so the whole value is read whole.
		class MapBuilder
		{
		public:
			explicit MapBuilder(uint64 words) : m_Bits((words + 63) / 64, 0), m_Words(words) {}

			bool Add(const Type* type, uint64 offset)
			{
				type = WithoutAliases(type);
				if (!type) return false;
				if (!TypeHandle::MayHoldPointers(type)) return true;
				switch (type->GetKind())
				{
				case TypeKind::Array: return AddArray(static_cast<const ArrayType*>(type), offset);
				case TypeKind::Object: return AddObject(static_cast<const ObjectType*>(type), offset);
				case TypeKind::Union: return AddFields(static_cast<const UnionType*>(type)->GetFields(), offset);
				default: return AddBytes(offset, type->GetSize()); // a pointer, a reference, a func, ...: every word of it
				}
			}

			std::vector<uint64> Take() { return std::move(m_Bits); }

			bool All() const
			{
				for (uint64 word = 0; word < m_Words; ++word)
					if (!(m_Bits[word / 64] >> (word % 64) & 1)) return false;
				return true;
			}

		private:
			bool AddArray(const ArrayType* array, uint64 offset)
			{
				const uint64 stride = array->GetElementType() ? array->GetElementType()->GetSize() : 0;
				if (!stride) return false;
				for (uint64 i = 0; i < array->GetElementCount(); ++i)
					if (!Add(array->GetElementType(), offset + i * stride)) return false;
				return true;
			}

			// Bases at their offsets, then the fields - an anonymous union's members each at its own offset, so a word
			// counts when any member's reading of it may hold a pointer.
			bool AddObject(const ObjectType* object, uint64 offset)
			{
				if (!object->GetSize()) return false;
				const auto& bases = object->GetBaseTypes();
				for (uint32 i = 0; i < bases.Count(); ++i)
					if (!bases[i] || !Add(bases[i], offset + object->GetBaseOffset(i))) return false;
				return AddFields(object->GetFields(), offset);
			}

			bool AddFields(const Core::Vector<ObjectType::FieldInfo>& fields, uint64 offset)
			{
				for (const ObjectType::FieldInfo& field : fields)
					if (!field.m_IsStatic && !Add(field.m_Type, offset + field.m_Offset)) return false;
				return true;
			}

			// Every word [offset, offset + size) overlaps: a pointer is only ever read from an aligned word.
			bool AddBytes(uint64 offset, uint64 size)
			{
				if (!size) return false;
				for (uint64 word = offset / kWordBytes; word * kWordBytes < offset + size && word < m_Words; ++word)
					m_Bits[word / 64] |= uint64(1) << (word % 64);
				return true;
			}

			std::vector<uint64> m_Bits;
			uint64 m_Words;
		};
	}

	std::optional<PointerMap> PointerMap::Of(const Type* type)
	{
		type = WithoutAliases(type);
		const uint64 bytes = type ? type->GetSize() : 0;
		if (!bytes || bytes % kWordBytes || bytes / kWordBytes > UINT32_MAX) return std::nullopt;
		MapBuilder builder(bytes / kWordBytes);
		if (!builder.Add(type, 0) || builder.All()) return std::nullopt;
		PointerMap map;
		map.words = static_cast<uint32>(bytes / kWordBytes);
		map.bits = builder.Take();
		return map;
	}

	llvm::Constant* PointerMap::Emit(llvm::Module& module) const
	{
		llvm::Constant* words = llvm::ConstantDataArray::get(module.getContext(), llvm::ArrayRef<uint64_t>(reinterpret_cast<const uint64_t*>(bits.data()), bits.size()));
		auto* global = new llvm::GlobalVariable(module, words->getType(), true, llvm::GlobalValue::PrivateLinkage, words, "c4.pointer_map");
		global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
		return global;
	}
}
