#include "Backend/CodeGen/Abi.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h" // pure packed vectors are SIMD scalars, not by-value aggregates
#include "Model/Types/SwizzleType.h"
// IntegerType / PointerType live in DerivedTypes.h on this vendored LLVM 22 (no individual headers).
#include <array>

namespace Scripting
{
	namespace
	{
		// The base type an aggregate question is really about: unwrap ALIASES only. References are deliberately
		// NOT unwrapped - a T& is passed by address (a pointer), never as a by-value aggregate, so it must not be
		// classified as one here. Pointers are likewise left alone (a pointer to a 12-byte struct is still a
		// scalar pointer in one register).
		const Type* AggregateBase(const Type* t)
		{
			if (!t) return nullptr;
			while (t && t->GetKind() == TypeKind::Alias)
				t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t;
		}

		// --- System V classification (the x86-64 psABI, section 3.2.3) -------------------------------------
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

	// A packed vector: `packed float[8]`, or an object that is one. It lowers to <N x T>.
	static bool IsPackedVector(const Type* b)
	{
		const TypeKind k = b->GetKind();
		return (k == TypeKind::Object && static_cast<const ObjectType*>(b)->IsPurePackedVector()) ||
			(k == TypeKind::Swizzle && static_cast<const SwizzleType*>(b)->IsPacked());
	}

	bool Abi::IsByValueAggregate(const Type* t) const
	{
		const Type* b = AggregateBase(t);
		if (!b) return false;
		TypeKind k = b->GetKind();
		// A packed vector passes in vector registers; on System V only when one register holds it, as clang passes
		// __m256 without AVX in memory.
		if (IsPackedVector(b)) return m_System == TargetSystem::Linux && b->GetSize() > m_VectorBytes();
		return k == TypeKind::Object || k == TypeKind::Union || k == TypeKind::Array;
	}

	llvm::Type* Abi::RegisterType(const Type* t) const
	{
		if (!IsByValueAggregate(t)) return nullptr;
		return m_System == TargetSystem::Linux ? SystemVRegisterType(t) : WindowsRegisterType(t);
	}

	llvm::Type* Abi::WindowsRegisterType(const Type* t) const
	{
		const size_t s = AggregateBase(t)->GetSize();
		if (s == 1 || s == 2 || s == 4 || s == 8) return llvm::IntegerType::get(m_Ctx, static_cast<unsigned>(s * 8)); // one GPR (bytes -> bits)
		return nullptr; // not a single-register size -> hidden pointer
	}

	llvm::Type* Abi::SystemVRegisterType(const Type* t) const
	{
		const uint64_t size = AggregateBase(t)->GetSize();
		if (size == 0 || size > kMostInRegisters) return nullptr;
		llvm::Type* storage = m_Lower(t);
		Classes classes = { Class::None, Class::None };
		if (!storage || !Classify(m_Layout, storage, 0, classes)) return nullptr;
		llvm::Type* low = EightbyteType(m_Ctx, m_Layout, storage, classes[0], 0, size);
		if (size <= kEightbyte) return low;
		return llvm::StructType::get(m_Ctx, { low, EightbyteType(m_Ctx, m_Layout, storage, classes[1], kEightbyte, size) });
	}

	bool Abi::PassesInRegister(const Type* t) const
	{
		if (!t) return false;
		return !IsByValueAggregate(t) || RegisterType(t) != nullptr;
	}

	llvm::Type* Abi::ParamType(const Type* t) const
	{
		if (llvm::Type* registers = RegisterType(t)) return registers; // the aggregate's bytes in registers
		if (PassesByHiddenPtr(t)) return llvm::PointerType::get(m_Ctx, 0); // hidden ptr to the caller's temporary
		return nullptr; // scalar: use ModuleContext::Lower
	}

	bool Abi::IsSretReturn(const Type* t, CallKind kind) const
	{
		if (!IsByValueAggregate(t)) return false;
		const Type* b = AggregateBase(t);
		if (IsPackedVector(b)) return false; // a wide vector returns in the registers LLVM splits it into, as clang's does
		if (!RegisterType(t)) return true;
		const bool objectType = b->GetKind() == TypeKind::Object || b->GetKind() == TypeKind::Union;
		return m_System == TargetSystem::Windows && kind == CallKind::Member && objectType;
	}

	Abi::Signature Abi::SignatureOf(const Type* ret, CallKind kind) const
	{
		Signature sig;
		sig.sret = IsSretReturn(ret, kind);
		const bool sretFirst = kind == CallKind::Extension || m_System == TargetSystem::Linux;
		unsigned next = 0;
		if (sig.sret && sretFirst) sig.sretIndex = static_cast<int>(next++);
		if (kind != CallKind::Free) sig.thisIndex = static_cast<int>(next++);
		if (sig.sret && !sretFirst) sig.sretIndex = static_cast<int>(next++);
		sig.firstParam = next;
		return sig;
	}

	llvm::Type* Abi::ReturnType(const Type* t, CallKind kind) const
	{
		if (IsSretReturn(t, kind)) return llvm::Type::getVoidTy(m_Ctx); // hidden out-ptr + void return
		return RegisterType(t); // an aggregate returned in registers; null for a scalar / reference: use ModuleContext::Lower
	}

	llvm::Type* Abi::CoercionSlot(llvm::Type* object, llvm::Type* registers) const
	{
		return m_Layout.getTypeAllocSize(registers) > m_Layout.getTypeAllocSize(object) ? registers : object;
	}
} // namespace Scripting
