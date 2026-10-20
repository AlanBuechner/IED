#include "Backend/CodeGen/Abi.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ObjectType.h" // pure packed vectors are SIMD scalars, not by-value aggregates
// IntegerType / PointerType live in DerivedTypes.h on this vendored LLVM 22 (no individual headers).

namespace Scripting
{
	// The base type an aggregate question is really about: unwrap ALIASES only. References are deliberately
	// NOT unwrapped - a T& is passed by address (a pointer), never as a by-value aggregate, so it must not be
	// classified as one here. Pointers are likewise left alone (a pointer to a 12-byte struct is still a
	// scalar pointer in one register).
	static const Type* AggregateBase(const Type* t)
	{
		if (!t) return nullptr;
		while (t && t->GetKind() == TypeKind::Alias)
			t = static_cast<const AliasType*>(t)->GetAliasedType();
		return t;
	}

	bool Abi::IsByValueAggregate(const Type* t) const
	{
		const Type* b = AggregateBase(t);
		if (!b) return false;
		TypeKind k = b->GetKind();
		if (k == TypeKind::Object && static_cast<const ObjectType*>(b)->IsPurePackedVector())
			return false; // a pure packed vector lowers to <N x T> and passes in XMM registers, not sret
		return k == TypeKind::Object || k == TypeKind::Union || k == TypeKind::Array;
	}

	uint32_t Abi::ByValueIntSize(const Type* t) const
	{
		if (!IsByValueAggregate(t)) return 0;
		size_t s = AggregateBase(t)->GetSize();
		if (s == 1 || s == 2 || s == 4 || s == 8) return static_cast<uint32_t>(s);
		return 0; // not a single-register size -> hidden pointer
	}

	bool Abi::PassesInRegister(const Type* t) const
	{
		if (!t) return false;
		return !IsByValueAggregate(t) || ByValueIntSize(t) != 0;
	}

	llvm::Type* Abi::ParamType(const Type* t) const
	{
		uint32_t s = ByValueIntSize(t);
		if (s != 0) return llvm::IntegerType::get(m_Ctx, s * 8); // aggregate riding in one GPR as an int of that width (bytes -> bits)
		if (PassesByHiddenPtr(t)) return llvm::PointerType::get(m_Ctx, 0); // hidden ptr to the caller's temporary
		return nullptr; // scalar: use ModuleContext::Lower
	}

	bool Abi::IsSretReturn(const Type* t, CallKind kind) const
	{
		if (!IsByValueAggregate(t)) return false;
		const Type* b = AggregateBase(t);
		bool objectType = b->GetKind() == TypeKind::Object || b->GetKind() == TypeKind::Union;
		return ByValueIntSize(t) == 0 || (kind == CallKind::Member && objectType);
	}

	Abi::Signature Abi::SignatureOf(const Type* ret, CallKind kind) const
	{
		Signature sig;
		sig.sret = IsSretReturn(ret, kind);
		unsigned next = 0;
		if (kind == CallKind::Member) sig.thisIndex = static_cast<int>(next++);
		if (sig.sret) sig.sretIndex = static_cast<int>(next++);
		if (kind == CallKind::Extension) sig.thisIndex = static_cast<int>(next++);
		sig.firstParam = next;
		return sig;
	}

	llvm::Type* Abi::ReturnType(const Type* t, CallKind kind) const
	{
		if (IsSretReturn(t, kind)) return llvm::Type::getVoidTy(m_Ctx); // hidden out-ptr + void return
		uint32_t s = ByValueIntSize(t);
		if (s != 0) return llvm::IntegerType::get(m_Ctx, s * 8); // aggregate returned in one GPR (bytes -> bits)
		return nullptr; // scalar / reference: use ModuleContext::Lower
	}
} // namespace Scripting
