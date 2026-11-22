#include "Backend/CodeGen/Abi.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h" // pure packed vectors are SIMD scalars, not by-value aggregates
#include "Model/Types/SwizzleType.h"
// IntegerType / PointerType live in DerivedTypes.h on this vendored LLVM 22 (no individual headers).

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
		if (IsPackedVector(b)) return m_Target.VectorPassesInMemory(b->GetSize(), m_VectorBytes());
		return k == TypeKind::Object || k == TypeKind::Union || k == TypeKind::Array;
	}

	llvm::Type* Abi::RegisterType(const Type* t) const
	{
		if (!IsByValueAggregate(t)) return nullptr;
		return m_Target.AggregateRegisters({ m_Ctx, m_Layout, AggregateBase(t)->GetSize(), [&] { return m_Lower(t); }, m_ManagedSpace });
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
		return m_Target.MemberReturnsObjectsInMemory() && kind == CallKind::Member && objectType;
	}

	Abi::Signature Abi::SignatureOf(const Type* ret, CallKind kind) const
	{
		Signature sig;
		sig.sret = IsSretReturn(ret, kind);
		const bool sretFirst = kind == CallKind::Extension || m_Target.SretBeforeThis();
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
