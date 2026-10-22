#include "Model/OverloadResolution.h"
#include <algorithm>
#include <vector>
#include "Model/ConversionAdvice.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h" // UnwrapToValue brick (moved off the Type base)
#include "Model/Types/ObjectType.h"
#include "Model/Types/ClassHierarchy.h" // derived -> base conversions through any base
#include "Model/Types/PointerType.h"
#include "Model/Types/SwizzleType.h" // packed swizzle binds a by-value packed-vector param
#include "Model/Types/SignatureType.h"
#include "Model/Types/AggregateSlots.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/BracedListType.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Model/Types/ReferenceType.h"
#include "Model/Types/EnumType.h"

namespace Scripting
{
	// Pointer-kind / universal-handle predicates shared by ParamCost. A "universal" handle is void*
	// (and recursively any pointer whose pointee is itself universal): in C4's one-opaque-ptr model
	// every pointer lowers to the SAME LLVM ptr, so void** / T** are interchangeable - the COM
	// out-param idiom `D3D12CreateDevice(..., &g_dev)` passes ID3D12Device** into a void** parameter.
	// There is no built-in string type: C strings are char*, which binds to void* params through this rule.
	static bool PtrKind(const Type* t)
	{
		if (!t) return false;
		TypeKind k = t->GetKind();
		return k == TypeKind::Pointer || k == TypeKind::FunPtr;
	}

	static bool IsUniversalHandle(const Type* t)
	{
		if (!t || t->GetKind() != TypeKind::Pointer) return false; // a funcptr has no pointee
		const auto* pt = static_cast<const PointerType*>(t);
		const Type* pe = pt->GetPointeeType();
		return pe && (pe->GetKind() == TypeKind::Void || IsUniversalHandle(pe));
	}

	// The call operator of `obj` with exactly `target`'s signature, or null.
	static const ObjectType::MethodInfo* CallOperatorFor(const ObjectType* obj, const SignatureType* target)
	{
		for (const auto& m : obj->GetMethods())
			if (m.HasOperator("call") && target->HasSignature(m.m_ReturnType, m.m_ParameterTypes)) return &m;
		return nullptr;
	}

	// Callables (docs/language/07): a func of a signature takes a funcptr, a func or an object with a call
	// operator of that signature, and null; a funcptr takes an object whose matching call operator is static
	// (a lambda that captures nothing). -1 when no such conversion applies.
	static int CallableCost(const SignatureType* target, const Type* arg)
	{
		const bool toFunc = target->GetKind() == TypeKind::Func;
		if (const auto* source = dynamic_cast<const SignatureType*>(arg))
			return target->HasSignature(source->GetReturnType(), source->GetParameterTypes()) &&
				(toFunc || source->GetKind() == TypeKind::FunPtr) ? (source->GetKind() == target->GetKind() ? 0 : 1) : -1;
		if (toFunc && IsUniversalHandle(arg)) return 1; // null
		const auto* obj = TypeAs<ObjectType>(arg);
		const ObjectType::MethodInfo* call = obj ? CallOperatorFor(obj, target) : nullptr;
		if (!call) return -1;
		return toFunc || call->m_IsStatic ? 1 : -1;
	}

	namespace
	{
		// The cost of building a parameter's object or array from braces: below a conversion's that loses
		// nothing, above an exact or derived match (D-57).
		constexpr int kBracedBuildCost = 3;
		// A scalar broadcast into a packed vector (D-62) costs more than its conversion to the element: a scalar
		// overload of the same function wins.
		constexpr int kBroadcastCost = 3;

		// `__init_list<T>`, or a pointer to one: the const-reference FFI form, where C++ `const
		// std::initializer_list<T>&` maps to `__init_list<T>*` and the caller passes the list it builds.
		bool IsInitListParameter(const Type* param)
		{
			if (param->GetKind() == TypeKind::InitList) return true;
			if (param->GetKind() != TypeKind::Pointer) return false;
			const Type* pointee = static_cast<const PointerType*>(param)->GetPointeeType();
			return pointee && pointee->GetKind() == TypeKind::InitList;
		}

		// A create taking `entries` arguments or the list itself, when the type has creates; else at least `entries`
		// slots.
		bool BracesBuild(const ObjectType* type, uint32 entries)
		{
			bool hasCreates = false;
			for (const auto& method : type->GetMethods())
			{
				if (method.m_IsStatic || !method.HasOperator("create")) continue;
				hasCreates = true;
				const uint32 count = method.m_ParameterTypes.Count();
				const Type* only = count == 1 ? TypeHandle::UnwrapToValue(method.m_ParameterTypes[0]) : nullptr;
				if (only && only->GetKind() == TypeKind::InitList) return true;
				const auto* written = dynamic_cast<const FunctionDefinitionNode*>(method.m_DeclaringNode);
				const uint32 minimum = written ? count - written->TrailingDefaultCount() : count;
				if (entries >= minimum && entries <= count) return true;
			}
			if (hasCreates) return false;
			if (type->IsPurePackedVector()) return entries <= type->GetPackedVectorSwizzle()->GetLaneCount();
			return entries <= AggregateSlots::Of(*type).size();
		}
	}

	// A braced argument `{...}` has no type of its own, only its entries (D-57). It binds an `__init_list<T>` (or a
	// pointer to one) exactly, and costs kBracedBuildCost against an object parameter, by value or `const T&`, that
	// its entries build, or an array parameter of at least as many elements. Nothing else takes it.
	int OverloadResolver::BracedCost(const Type* param, uint32 entries)
	{
		if (IsInitListParameter(param)) return 0;
		const Type* declared = param;
		while (declared && declared->GetKind() == TypeKind::Alias) declared = static_cast<const AliasType*>(declared)->GetAliasedType();
		if (declared && declared->GetKind() == TypeKind::Reference && !static_cast<const ReferenceType*>(declared)->IsConstReferent()) return -1;
		const Type* value = TypeHandle::UnwrapToValue(param);
		if (const auto* array = TypeAs<ArrayType>(value)) return array->GetElementCount() >= entries ? kBracedBuildCost : -1;
		if (const auto* object = TypeAs<ObjectType>(value)) return BracesBuild(object, entries) ? kBracedBuildCost : -1;
		return -1;
	}

	int OverloadResolver::ParamCost(const Type* param, const Type* arg)
	{
		if (!param) return -1;

		// A function template's name binds a funcptr whose signature has an instance (the function's address,
		// exact), or a func through that address (D-17).
		if (const auto* named = TypeAs<TemplateNameType>(arg))
		{
			const auto* signature = dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(param));
			if (!signature || !named->Instance(*signature).Ok()) return -1;
			return signature->GetKind() == TypeKind::FunPtr ? 0 : 1;
		}

		if (const auto* braces = TypeAs<BracedListType>(arg)) return BracedCost(param, braces->GetEntryCount());
		if (!arg) return -1;

		const Type* p = TypeHandle::UnwrapToValue(param); // aliases are transparent in matching
		const Type* a = TypeHandle::UnwrapToValue(arg);

		if (p == a) return 0; // exact match: pointer identity after alias unwrap
		if (const auto* signature = dynamic_cast<const SignatureType*>(p))
			if (int cost = CallableCost(signature, a); cost >= 0) return cost;

		// A derived object binds to any of its bases (cost 1): a reference names the base sub-object, a by-value
		// parameter takes a copy of it. Only aliases are stripped from the parameter here: UnwrapToValue also
		// strips the reference.
		const Type* declared = param;
		while (declared && declared->GetKind() == TypeKind::Alias) declared = static_cast<const AliasType*>(declared)->GetAliasedType();
		const auto* baseObj = TypeAs<ObjectType>(p);
		const auto* argObj = TypeAs<ObjectType>(a);
		if (baseObj && argObj)
			return ClassHierarchy::DerivesFrom(argObj, baseObj) ? 1 : -1;
		// A non-const reference names the argument itself, so it binds only its own type (D-32); a const one
		// takes any argument that converts to its referent, through a temporary.
		if (declared && declared->GetKind() == TypeKind::Reference && !static_cast<const ReferenceType*>(declared)->IsConstReferent())
			return -1;

		// D-32: an enum class converts from and to integers only through a cast; a plain enum takes an integer
		// of its underlying type (cost 1) and converts to any number.
		auto scoped = [](const Type* t) { return t->GetKind() == TypeKind::Enum && static_cast<const EnumType*>(t)->IsScoped(); };
		if (scoped(p) || scoped(a)) return -1;
		if (p->GetKind() == TypeKind::Enum)
		{
			const Type* underlying = static_cast<const EnumType*>(p)->GetUnderlyingType();
			if (underlying && TypeHandle::UnwrapToValue(underlying) == a) return 1;
			return -1;
		}

		// Implicit numeric conversion: any integral/floating argument to any integral/floating param.
		// Same category (integral->integral, floating->floating) is a cheaper conversion than crossing
		// categories (integral<->floating), so `At(1)` prefers the uint32 overload over the float one
		// instead of being ambiguous.
		if (p->IsNumeric() && a->IsNumeric())
			return (p->IsIntegral() == a->IsIntegral()) ? 1 : 2;

		// Pointer coercion (C semantics): every C4 pointer lowers to ONE opaque LLVM ptr, but only the
		// UNIVERSAL handles - void* and string - may bind across pointee types. Any pointer converts to a
		// void*/string parameter (char* -> void*, T* -> string), and a void*/string argument converts to any
		// pointer parameter ((T*)raw_alloc(...)). Specific-to-specific still requires the exact type, so an
		// unqualified call that matches both a free function and a method of the same mangled name does not
		// become ambiguous.
		auto ptrKind = [](const Type* t) { return PtrKind(t); };
		auto universal = [](const Type* t) { return IsUniversalHandle(t); };
		if (ptrKind(p) && ptrKind(a) && (universal(p) || universal(a))) return 1;

		// Array decay (C): an array argument binds a pointer to its element type, const or not, or `void*`.
		if (p->GetKind() == TypeKind::Pointer && a->GetKind() == TypeKind::Array)
		{
			const Type* pe = TypeHandle::UnwrapToValue(static_cast<const PointerType*>(p)->GetPointeeType());
			const Type* ae = TypeHandle::UnwrapToValue(static_cast<const ArrayType*>(a)->GetElementType());
			if (pe == ae || (pe && pe->GetKind() == TypeKind::Void)) return 1;
		}

		// Pointer-qualification conversion (C standard): T* binds to a `const T*` parameter - adding const
		// is free (same opaque LLVM ptr). This is what lets a string literal (char*) pass to a `const char*`
		// message parameter. Dropping const (const T* -> T*) is NOT allowed, so the rule is one-directional.
		if (p->GetKind() == TypeKind::Pointer && a->GetKind() == TypeKind::Pointer)
		{
			const auto* pp = static_cast<const PointerType*>(p);
			const auto* aa = static_cast<const PointerType*>(a);
			if (pp->IsConstReferent() && !aa->IsConstReferent() && pp->GetPointeeType() == aa->GetPointeeType())
				return 1;
		}

		// Pointer covariance (C++ polymorphism): a derived object pointer upcasts to any of its bases -
		// Circle* binds a Shape* parameter, Impl* an IRenderable*. The conversion adjusts the pointer to the
		// base sub-object (ClassHierarchy::PointerDelta at emission).
		if (p->GetKind() == TypeKind::Pointer && a->GetKind() == TypeKind::Pointer)
		{
			const auto* pp = static_cast<const PointerType*>(p)->GetPointeeType();
			const auto* aa = static_cast<const PointerType*>(a)->GetPointeeType();
			const auto* pobj = TypeAs<ObjectType>(TypeHandle::UnwrapToValue(pp));
			const auto* aobj = TypeAs<ObjectType>(TypeHandle::UnwrapToValue(aa));
			bool constOk = static_cast<const PointerType*>(p)->IsConstReferent() || !static_cast<const PointerType*>(a)->IsConstReferent();
			if (pobj && aobj && constOk && ClassHierarchy::DerivesFrom(aobj, pobj)) return 1;
		}

		// Packed SIMD: a swizzle sub-vector (v.xz / r.wwww) has the SAME LLVM type (<N x T>) as the packed
	// vector object it came from, so it binds to a by-value packed-vector parameter of matching element
	// and width - `PVLaneSum4(r.wwww)` passes the splat as a PackedVec4. Cost 1 (a conversion).
	if (p->GetKind() == TypeKind::Object && a->GetKind() == TypeKind::Swizzle)
	{
		const auto* pobj = static_cast<const ObjectType*>(p);
		if (pobj->IsPurePackedVector())
		{
			const SwizzleType* psw = pobj->GetPackedVectorSwizzle();
			const SwizzleType* asw = static_cast<const SwizzleType*>(a);
			if (psw && asw && psw->GetLaneCount() == asw->GetLaneCount() && psw->GetElementType() == asw->GetElementType())
				return 1;
		}
	}

	if (const auto* lanes = TypeAs<SwizzleType>(p); lanes && lanes->IsPacked() && a->IsNumeric())
	{
		const int element = ParamCost(lanes->GetElementType(), a);
		return element < 0 ? -1 : element + kBroadcastCost;
	}

	return -1; // not viable: no implicit path between these types
	}

	namespace
	{
		bool FromFunctionTemplate(const FunctionRecord* rec)
		{
			return rec && rec->templateOrigin == TemplateOrigin::FunctionTemplate;
		}
	}

	OverloadResolution OverloadResolver::Resolve(const Core::Vector<OverloadCandidate>& candidates,
		const Core::Vector<const Type*>& args, const TemplateOrdering& moreSpecialized)
	{
		if (candidates.Empty()) return { OverloadResolution::Status::NoMatch, nullptr, nullptr, "no candidates" };

		// The viable candidates with the lowest total cost.
		int bestCost = -1;
		std::vector<const OverloadCandidate*> finalists;
		for (const auto& c : candidates)
		{
			const int cost = CandidateCost(c, args);
			if (cost < 0) continue;
			if (bestCost == -1 || cost < bestCost) { bestCost = cost; finalists.clear(); }
			if (cost == bestCost) finalists.push_back(&c);
		}
		if (finalists.empty()) return { OverloadResolution::Status::NoMatch, nullptr, nullptr, "no viable overload for these arguments" };

		// An equally good function template instance loses to a function written as it is (D-17); between
		// template instances the more specialized template wins (partial ordering).
		auto isTemplate = [](const OverloadCandidate* c) { return FromFunctionTemplate(c->record); };
		if (std::any_of(finalists.begin(), finalists.end(), [&](const OverloadCandidate* c) { return !isTemplate(c); }))
			finalists.erase(std::remove_if(finalists.begin(), finalists.end(), isTemplate), finalists.end());
		else if (moreSpecialized)
		{
			std::vector<const OverloadCandidate*> kept;
			for (const OverloadCandidate* c : finalists)
				if (std::none_of(finalists.begin(), finalists.end(), [&](const OverloadCandidate* o) { return o != c && moreSpecialized(o->record, c->record) > 0; }))
					kept.push_back(c);
			finalists = kept;
		}

		// Written functions of one signature are one function seen twice (an override and its base method):
		// the first, most derived, is it. Template instances of one signature are different templates.
		const OverloadCandidate* chosen = finalists[0];
		for (const OverloadCandidate* c : finalists)
		{
			const bool same = isTemplate(c) ? c->record == chosen->record : c->sig == chosen->sig;
			if (!same)
				return { OverloadResolution::Status::Ambiguous, nullptr, nullptr,
					"ambiguous call - equally good overloads: " + chosen->sig->ToString() + " and " + c->sig->ToString() };
		}
		return { OverloadResolution::Status::Resolved, chosen->sig, chosen->record, "" };
	}

	namespace
	{
		Core::String Arguments(uint32 count) { return std::to_string(count) + (count == 1 ? " argument" : " arguments"); }

		Core::String ArityOf(const OverloadCandidate& c)
		{
			const uint32 most = c.sig->GetParameterCount();
			if (c.sig->IsVariadic()) return "at least " + Arguments(c.minArity);
			if (c.minArity == most) return Arguments(most);
			return std::to_string(c.minArity) + " to " + Arguments(most);
		}
	}

	CandidateMismatch OverloadResolver::Explain(const OverloadCandidate& c, const Core::Vector<const Type*>& args, const Core::String& name)
	{
		if (!c.sig) return {};
		const uint32 paramCount = c.sig->GetParameterCount();
		if (args.Count() < c.minArity || (args.Count() > paramCount && !c.sig->IsVariadic()))
			return { -1, name + " takes " + ArityOf(c) + ", not " + std::to_string(args.Count()) };
		for (uint32 i = 0; i < args.Count() && i < paramCount; ++i)
		{
			const FunctionType::ParameterInfo& param = c.sig->GetParameters()[i];
			if (!args[i] || ParamCost(param.m_Type, args[i]) >= 0) continue;
			const Core::String paramName = c.record && i < c.record->paramNames.Count() ? c.record->paramNames[i] : param.mName;
			const Core::String named = paramName.empty() ? "" : "'" + paramName + "' ";
			return { (int32)i, "argument " + std::to_string(i + 1) + " of " + name + " has type '" + args[i]->GetName() +
				"' but parameter " + named + "expects '" + param.m_Type->GetName() + "'" + ConversionAdvice(args[i], param.m_Type) };
		}
		return {};
	}

	int OverloadResolver::CandidateCost(const OverloadCandidate& c, const Core::Vector<const Type*>& args)
	{
		// The sum of the arguments' conversion costs; -1 when the candidate is not viable.
		if (!c.sig) return -1;
		const uint32 paramCount = c.sig->GetParameterCount();
		if (args.Count() < c.minArity) return -1;                          // too few for the non-defaulted params
		if (args.Count() > paramCount && !c.sig->IsVariadic()) return -1;   // too many (a C variadic accepts any number past its params)
		int total = 0;
		for (uint32 i = 0; i < args.Count(); ++i)
		{
			// A variadic argument past the named params binds as-is but costs a little: an EXACT non-variadic
			// overload must beat a variadic one for the same arguments (F(int32,int32) over F(int32,...)).
			if (i >= paramCount) { total += 1; continue; }
			const int cost = ParamCost(c.sig->GetParameters()[i].m_Type, args[i]);
			if (cost < 0) return -1;
			total += cost;
		}
		return total;
	}

} // namespace Scripting
