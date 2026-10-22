#include "Model/Builder/ModelResolver.h"
#include "Model/Builder/LinkSymbol.h"
#include "Model/Builder/ModelBuilder.h" // Context + pending work-lists (friend access)
#include "Model/Builder/FreeFunctionResolver.h" // free-function pass-2 resolver (delegated from ResolveRound)
#include "Model/TypeHandle.h" // UnwrapToValue brick (moved off the Type base)
#include "Model/Types/ClassHierarchy.h"
#include "Model/IntegerRanges.h"
#include "Model/OperatorAnnotations.h" // [operator(...)] set collection (shared with instantiation)
#include "Compiler/CompilerInstance.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include <set>
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/SwizzleType.h" // packed/unpacked SIMD region types (pass-2 swizzle resolution)
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h" // complete type: function bodies are stored as pointers
#include "Parser/Nodes/Declarations/QualTypeNode.h"  // GAP A body walk reads specialization types
#include "Model/ConstEvaluator.h"
#include <functional>

namespace Scripting
{
	// ===========================================================================
	// PASS 2 - RESOLVE (doc 02). Fixed point over the pending lists. Each resolver settles what it
	// can this round and leaves the rest for the next; a name always resolves to something that was
	// registered in pass 1, so forward/cross-file/mutual references converge. Returns true when any
	// resolver made progress (the loop stops when a full round changes nothing).
	// ===========================================================================
	bool ModelResolver::ResolveRound(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;
		changed |= ResolveVariables(builder, ctx);
		changed |= ResolveEnums(builder, ctx);
		changed |= ResolveAliases(builder, ctx);
		changed |= ResolveUnions(builder, ctx);
		changed |= ResolveObjectMembers(builder, ctx);
		changed |= FreeFunctionResolver(builder, ctx).Resolve(); // unqualified + qualified (namespace/class) functions
		changed |= ConstEvaluator::FoldGlobalConstants(*ctx.model);
		return changed;
	}

	bool ModelResolver::ResolveVariables(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;
		for (uint32 i = 0; i < builder.m_PendingVars.Count(); )
		{
			ModelBuilder::PendingVar& v = builder.m_PendingVars[i];
			// In the scope it is declared in, so `S g;` inside `namespace N` names N::S (BUG-186).
			auto t = ctx.model->ResolveTypeInContext(*v.var->GetType(), v.unitId, v.info->declaringScope);
			if (!t.Ok()) { ++i; continue; } // defer: the type may register later this round
			v.info->type = t.value;
			builder.m_PendingVars.Remove(i);
			changed = true;
		}
		return changed;
	}

	// An initializer sees the members before it by their bare names (`B = A + 1`), as in C++: they are bound
	// constants of a layer the evaluator looks in first.
	bool ModelResolver::MemberValues(const ProgramModel& model, ModelBuilder::PendingEnum& e, Core::Vector<int64>& values)
	{
		Scope earlier(Scope::Kind::Block, "enum-members");
		int64 next = 0;
		for (const auto& member : e.en->GetMembers())
		{
			int64 value = next;
			if (member->HasInitializer())
			{
				auto folded = ConstEvaluator(model, e.scope).Evaluate(member->GetInitializer().Get(), e.unitId, &earlier);
				if (!folded.Ok())
				{
					e.why = "member '" + member->GetNameToken().GetText() + "': " + folded.message;
					return false;
				}
				value = folded.value.AsInt();
			}
			values.Push(value);
			next = static_cast<int64>(static_cast<uint64>(value) + 1);
			SymbolInfo bound;
			bound.kind = SymbolKind::Variable;
			bound.isConst = true;
			bound.constValue = ConstantValue::Int(value);
			earlier.Register(member->GetNameToken().GetText(), bound);
		}
		return true;
	}

	// `enum E { X = 9999999999 }`: an int32 enum cannot hold X.
	void ModelResolver::ReportMembersOutOfRange(ModelBuilder& builder, const ModelBuilder::Context& ctx, const ModelBuilder::PendingEnum& e,
		const Core::Vector<int64>& values)
	{
		const Type* underlying = e.raw->GetUnderlyingType();
		for (uint32 m = 0; m < values.Count(); ++m)
		{
			if (IntegerRanges::Fits(values[m], underlying)) continue;
			const auto& member = e.en->GetMembers()[m];
			builder.ErrorAt(ctx, member.Get(), "the enum member '" + e.en->GetNameToken().GetText() + "::" + member->GetNameToken().GetText() + "' is " +
				std::to_string(values[m]) + ", outside the range of its underlying type '" + underlying->GetName() + "'");
		}
	}

	QueryResult<void> ModelResolver::AttachBase(const ObjectType& derived, const Type* base)
	{
		auto checked = ClassHierarchy::CheckCanInherit(&derived, base);
		if (checked.Ok()) const_cast<ObjectType&>(derived).AddBaseType(static_cast<const ObjectType*>(base));
		return checked;
	}

	bool ModelResolver::ResolveEnums(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;
		for (uint32 i = 0; i < builder.m_PendingEnums.Count(); )
		{
			ModelBuilder::PendingEnum& e = builder.m_PendingEnums[i];
			const EnumDefinitionNode& en = *e.en;

			// Underlying type first (may be a forward ref).
			if (en.HasUnderlyingType())
			{
				auto ut = ctx.model->ResolveTypeInContext(*en.GetUnderlyingType(), e.unitId, nullptr);
				if (!ut.Ok()) { ++i; continue; }
				e.raw->SetUnderlyingType(ut.value);
			}

			// Member values: evaluate each initializer; defer the whole enum if any is not yet a
			// constant (it may name a const or another enum that folds later this round).
			Core::Vector<int64> values;
			if (!MemberValues(*ctx.model, e, values)) { ++i; continue; }
			ReportMembersOutOfRange(builder, ctx, e, values);

			for (uint32 m = 0; m < en.GetMembers().Count(); ++m)
			{
				EnumType::MemberInfo mi;
				mi.mName = en.GetMembers()[m]->GetNameToken().GetText();
				mi.m_Value = values[m];
				e.raw->AddMember(mi);
			}
			builder.m_PendingEnums.Remove(i);
			changed = true;
		}
		return changed;
	}

	namespace
	{
		bool SameType(const Type* a, const Type* b)
		{
			return TypeHandle(a).ResolveAlias().Raw() == TypeHandle(b).ResolveAlias().Raw();
		}
	}

	bool ModelResolver::ResolveAliases(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;
		for (uint32 i = 0; i < builder.m_PendingAliases.Count(); )
		{
			ModelBuilder::PendingAlias& a = builder.m_PendingAliases[i];
			// Resolve the target in the ALIAS'S OWN scope: `alias IWedge = Wedge<int32>` inside
			// `namespace Weld` must bind Weld's Wedge, not a same-named global template. A null context
			// (a pre-scope-fix entry) falls back to root, as before.
			auto t = ctx.model->ResolveTypeInContext(*a.target, a.unitId, a.scope);
			if (!t.Ok()) { ++i; continue; }
			if (a.repeats && !a.repeats->GetAliasedType()) { ++i; continue; } // the first declaration resolves first
			a.raw->SetAliasedType(t.value);
			if (a.repeats && !SameType(t.value, a.repeats->GetAliasedType()))
				builder.ErrorAt(ctx, a.declaration, "redefinition of alias '" + a.raw->GetName() + "' as a different type: '" +
					t.value->ToString() + "', was '" + a.repeats->GetAliasedType()->ToString() + "'");
			builder.m_PendingAliases.Remove(i);
			changed = true;
		}
		return changed;
	}

	bool ModelResolver::ResolveUnions(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;
		for (uint32 i = 0; i < builder.m_PendingUnions.Count(); )
		{
			ModelBuilder::PendingUnion& u = builder.m_PendingUnions[i];
			bool allOk = true;
			Core::Vector<ObjectType::FieldInfo> fields;
			// In the scope the union is declared in, so `S s;` inside `namespace N` names N::S (BUG-191).
			ForEachUnionField(*u.un, [&](const VariableDefinitionNode& var, int view) {
				if (!allOk) return;
				auto vt = ctx.model->ResolveTypeInContext(*var.GetType(), u.unitId, u.scope);
				if (!vt.Ok()) { allOk = false; return; }
				ObjectType::FieldInfo f;
				f.mName = var.GetNameToken().GetText();
				f.m_Type = vt.value;
				f.m_IsConst = var.IsConst();
				f.m_IsVolatile = var.IsVolatile();
				f.m_Access = var.GetAccessModifier();
				f.m_DeclaringNode = &var;
				f.m_UnionViewId = view;
				fields.Push(f);
			});
			if (!allOk) { ++i; continue; }
			for (const auto& f : fields) u.raw->AddField(f);
			u.raw->CalculateLayout(); // named unions need a concrete size for sizeof / slot allocas
			builder.m_PendingUnions.Remove(i);
			changed = true;
		}
		return changed;
	}

	bool ModelResolver::ResolveObjectMembers(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		bool changed = false;

		// Fields: resolve the type in the class scope (so nested types and members are visible),
		// add to the object, and register statics in the class scope for `S::K` lookup.
		for (uint32 i = 0; i < builder.m_PendingFields.Count(); )
		{
			ModelBuilder::PendingField& f = builder.m_PendingFields[i];
			Scope* classScope = nullptr;
			auto scIt = ctx.model->m_TypeScopes.find(f.obj);
			if (scIt != ctx.model->m_TypeScopes.end()) classScope = const_cast<Scope*>(scIt->second);

			auto vt = ctx.model->ResolveTypeInContext(*f.var->GetType(), f.unitId, classScope);
			// A Success with a null type (e.g. an array whose size folded to 0) is not usable: keep it
			// pending so the end-of-pass report names it, instead of adding a null-typed field that would
			// later crash layout and codegen.
			if (!vt.Ok() || !vt.value) { ++i; continue; }
			if (f.var->IsThreadLocal() && !f.var->IsStatic())
				builder.ErrorAt(ctx, f.var, "field '" + f.var->GetNameToken().GetText() + "' cannot be thread_local: an object's fields live in the object; "
					"write 'static thread_local'");

			// A comma list (`int32 x, y;`) declares several FIELDS of the same type: add each.
			for (const auto& d : f.var->GetDeclarators())
			{
				ObjectType::FieldInfo fi;
				fi.mName = d.m_NameToken.GetText();
				fi.m_Type = vt.value;
				fi.m_IsStatic = f.var->IsStatic();
				fi.m_IsConst = f.var->IsConst();
				fi.m_IsVolatile = f.var->IsVolatile();
				fi.m_Access = f.var->GetAccessModifier();
				fi.m_DeclaringNode = f.var;
				if (!fi.m_IsStatic) fi.m_Initializer = d.m_Initializer.Get();
				// Swizzle (anonymous-union) lane: carry the region/view markers so CalculateLayout can
				// group them into one byte range. A plain lane has viewId -1 (aliases offset 0); a struct
				// view's lanes share a viewId >= 0 (sequential within the view).
				fi.m_InUnionRegion = f.unionRegionId >= 0;
				fi.m_UnionRegionId = f.unionRegionId;
				fi.m_UnionViewId = f.unionViewId;
				const_cast<ObjectType*>(f.obj)->AddField(fi);

			if (f.var->IsStatic() && classScope)
			{
				SymbolInfo sym;
				sym.kind = SymbolKind::Variable;
				sym.type = vt.value;
				sym.isConst = f.var->IsConst(); // only a `const` static is a compile-time constant; a plain static may be runtime-init
				sym.isStatic = true;
				sym.isVolatile = f.var->IsVolatile();
				sym.isThreadLocal = f.var->IsThreadLocal();
				sym.definingUnit = f.unitId;
				sym.declNode = f.var;
				sym.mangledName = CanonicalSymbolName(classScope, fi.mName);
				sym.linkSymbol = LinkSymbolOf(f.var->GetAnnotations()); // a bound C++ static data member is that variable
				sym.plainName = fi.mName; // plain C4 name for reflection (plan 14)
				for (const auto& ga : f.var->GetAnnotations()) CollectGroupAnnotations(ga.Get(), sym.groups);
				// Carry the initializer so FoldGlobalConstants can fold a constant one (`static int32 MaxCount = 8;`
				// must read 8, not 0). A non-constant initializer (a call) fails to fold and stays zero-init.
				sym.initializer = d.m_Initializer ? d.m_Initializer.Get() : nullptr;
				if (!sym.linkSymbol.empty() && sym.initializer) builder.ErrorAt(ctx, f.var, LinkedInitializerMessage(fi.mName, sym.linkSymbol));
				classScope->Register(fi.mName, sym);
			}
			}

			builder.m_PendingFields.Remove(i);
			changed = true;
		}

		// Packed/unpacked swizzle views (`union { packed float x, y, z; }`): build the SwizzleType
		// (the SIMD <N x T> region) and register it as a PackedViewRegion once its element type resolves.
		for (uint32 i = 0; i < builder.m_PendingSwizzles.Count(); )
		{
			ModelBuilder::PendingSwizzle& sw = builder.m_PendingSwizzles[i];
			auto et = ctx.model->ResolveTypeInContext(*sw.node->GetElementType().Get(), sw.unitId, nullptr);
			if (!et.Ok() || !et.value) { ++i; continue; } // element type not resolved yet
			// A `packed T[N] data` clause is the region's array view: its element is the lane type, N the width.
			auto laneType = [](const Type* t) {
				const auto* array = dynamic_cast<const ArrayType*>(t);
				return array ? TypeHandle::UnwrapToValue(array->GetElementType()) : t;
			};
			const Type* elem = laneType(TypeHandle::UnwrapToValue(et.value));
			// Co-located packed views of one anonymous union ALIAS the same SIMD region (x==r when both
			// name index 0), so they must merge into ONE SwizzleType: a `union { packed T x,y; packed T r,g; }`
			// is still a single <N x T> vector, not N separate regions. Merging keeps the object "pure" (one
			// region) so it lowers as the bare SIMD vector instead of an empty struct. Consecutive pending
			// entries with the same object + lane type + packed-ness are one union's clauses.
			uint32 j = i;
			while (j + 1 < builder.m_PendingSwizzles.Count())
			{
				ModelBuilder::PendingSwizzle& nxt = builder.m_PendingSwizzles[j + 1];
				if (nxt.obj != sw.obj || !nxt.node->IsPacked() || !sw.node->IsPacked()) break;
				auto nEt = ctx.model->ResolveTypeInContext(*nxt.node->GetElementType().Get(), nxt.unitId, nullptr);
				if (!nEt.Ok() || laneType(TypeHandle::UnwrapToValue(nEt.value)) != elem) break;
				++j;
			}
			SwizzleType* raw = new SwizzleType("packed", elem);
			raw->SetRegionPacked(sw.node->IsPacked());
			for (uint32 k = i; k <= j; ++k)
			{
				const SwizzleViewNode& view = *builder.m_PendingSwizzles[k].node;
				auto viewType = ctx.model->ResolveTypeInContext(*view.GetElementType().Get(), builder.m_PendingSwizzles[k].unitId, nullptr);
				if (const auto* array = dynamic_cast<const ArrayType*>(TypeHandle::UnwrapToValue(viewType.value)))
				{
					raw->SetVectorWidth(static_cast<uint32>(array->GetElementCount()));
					if (!view.GetLanes().Empty()) raw->SetArrayAlias(view.GetLanes()[0].GetText());
					continue;
				}
				uint32 index = 0; // per clause: x, y -> 0, 1 and r, g -> 0, 1 (they alias)
				for (const auto& lane : view.GetLanes()) raw->AddLaneAt(lane.GetText(), view.IsPacked(), index++);
			}
			raw->CalculateLayout(); // lay out the SIMD region before it is used (size/alignment)
			if (raw->IsPacked())
				if (Core::String why = SwizzleType::WhyNotAVector(elem, raw->GetLaneCount(), 1); !why.empty())
					builder.ErrorAt(ctx, sw.node, why);
			const SwizzleType* owned = ctx.model->m_Types->Own<SwizzleType>(raw);
			ObjectType* object = const_cast<ObjectType*>(sw.obj);
			if (raw->IsPacked()) object->AddPackedRegion(owned, sw.node);
			else object->AddPackedView(owned, owned->GetName());
			// An UNPACKED view is a struct-of-fields (doc: extractvalue/insertvalue per lane), not a SIMD
			// vector. Its lanes are NOT ordinary fields (FlattenSwizzle queued them as a region, not pending
			// fields), so materialize them here - each at its byte offset - so single-lane access and
			// aggregate init go through the normal field/struct machinery instead of an empty struct.
			if (!raw->IsPacked())
			{
				size_t esz = elem ? elem->GetSize() : 0;
				for (const auto& ln : raw->GetLanes())
				{
					ObjectType::FieldInfo fi;
					fi.mName = ln.m_Name;
					fi.m_Type = elem;
					fi.m_Offset = static_cast<size_t>(ln.m_Index) * esz; // lanes are sequential at the region base
					const_cast<ObjectType*>(sw.obj)->AddField(fi);
				}
				const_cast<ObjectType*>(sw.obj)->CalculateLayout();
			}
			for (uint32 k = i; k <= j; ++k) builder.m_PendingSwizzles.Remove(i); // drop the whole run
			changed = true;
		}

		// Base classes: resolve (instantiating template bases transparently) and attach. The base is
		// resolved in the DECLARING scope: `struct Circle : Shape<float>` inside a namespace names a
		// sibling of that namespace, so an unqualified first segment must not start from root.
		for (uint32 i = 0; i < builder.m_PendingBases.Count(); )
		{
			ModelBuilder::PendingBase& b = builder.m_PendingBases[i];
			auto bt = ctx.model->ResolveTypeInContext(*b.type, b.unitId, b.declScope);
			if (!bt.Ok()) { ++i; continue; }
			if (auto attached = AttachBase(*b.obj, TypeHandle::UnwrapToValue(bt.value)); !attached.Ok())
				builder.ErrorAt(ctx, b.type, attached.message);
			builder.m_PendingBases.Remove(i);
			changed = true;
		}

		// In-class method declarations: resolve the signature in the class scope, build the record,
		// attach to the object (MethodInfo) and the parallel record list, and register the name so
		// unqualified member calls and `S::Method` find it. Overloads grow the set (doc 08).
		for (uint32 i = 0; i < builder.m_PendingMethodDecls.Count(); )
		{
			ModelBuilder::PendingMethodDecl& d = builder.m_PendingMethodDecls[i];
			const FunctionDefinitionNode& fn = *d.fn;
			Scope* classScope = nullptr;
			auto scIt = ctx.model->m_TypeScopes.find(d.obj);
			if (scIt != ctx.model->m_TypeScopes.end()) classScope = const_cast<Scope*>(scIt->second);
			auto rt = ctx.model->ResolveTypeInContext(*fn.GetReturnType(), d.unitId, classScope);
			if (!rt.Ok()) { ++i; continue; }
			Core::Vector<FunctionType::ParameterInfo> params;
			bool ok = true;
			uint32 trailingDefaults = 0;
			for (const auto& p : fn.GetParameters())
			{
				auto pt = ctx.model->ResolveTypeInContext(*p->GetType(), d.unitId, classScope);
				if (!pt.Ok()) { ok = false; break; }
				FunctionType::ParameterInfo pi;
				pi.mName = p->HasName() ? p->GetNameToken().GetText() : ("arg" + std::to_string(params.Count()));
				pi.m_Type = pt.value;
				params.Push(pi);
				if (p->HasDefaultValue()) trailingDefaults++;
			}
			if (!ok) { ++i; continue; }

			const Core::String name = fn.GetName()->GetFirstIdentifierToken().GetText();
			Core::Vector<const Type*> paramTypes;
			for (const auto& p : params) paramTypes.Push(p.m_Type);
			const FunctionType* signature = ctx.model->m_Types->Function(rt.value, paramTypes, fn.IsVariadic()); // interned

			FunctionRecord rec;
			rec.type = signature;
			rec.scope = classScope ? const_cast<Scope*>(classScope) : nullptr;
			rec.containingType = d.obj;
				rec.bodyNode = fn.GetBody().Get();
				rec.declarations.Push(&fn);
				rec.definingUnit = d.unitId;
				rec.declUnit = d.unitId; // declared here: the visibility domain (stable)
		rec.isConst = fn.IsConstMethod();
		rec.mangledName = MethodSymbol(classScope, *d.obj, name, *static_cast<const FunctionType*>(signature), rec.isConst);
		rec.name = name; // plain C4 name for reflection (plan 14)
		for (const auto& ga : fn.GetAnnotations()) CollectGroupAnnotations(ga.Get(), rec.groups);
		rec.minArity = params.Count() > trailingDefaults ? params.Count() - trailingDefaults : 0;
		for (const auto& p : params) rec.paramNames.Push(p.mName); // names for the lowerer (FunctionType drops them)
		for (const auto& p : fn.GetParameters()) rec.paramDefaults.Push(p->GetDefaultValue().Get()); // default-arg exprs for the call lowerer
		rec.paramIsConst = fn.ParameterConstness();
			rec.isStatic = fn.IsStatic();
			rec.isVirtual = fn.IsVirtual();
			rec.isNoexcept = fn.IsNoexcept();
			rec.access = fn.GetAccessModifier();
			for (const auto& ann : fn.GetAnnotations())
				CollectOperatorAnnotations(ann.Get(), fn.GetParameters().Count(), rec.operatorAnnotations);

			const FunctionRecord* stored = ctx.model->Symbols()->AddFunctionRecord(rec);
			ctx.model->m_RecordBySignature.emplace(static_cast<const FunctionType*>(signature), stored);

			ObjectType::MethodInfo m;
			m.mName = name;
			m.m_ReturnType = rt.value;
			for (const auto& pi : params) m.m_ParameterTypes.Push(pi.m_Type);
			m.m_IsStatic = fn.IsStatic();
			m.m_IsVirtual = fn.IsVirtual();
			m.m_IsOverride = fn.IsOverride();
			m.m_IsFinal = fn.IsFinal();
			m.m_IsConst = fn.IsConstMethod();
			m.m_Access = fn.GetAccessModifier();
			m.m_DeclaringNode = &fn;
			m.m_HasBody = fn.GetBody() != nullptr;
			m.m_OperatorAnnotations = rec.operatorAnnotations;
			const_cast<ObjectType*>(d.obj)->AddMethod(m);
			ctx.model->m_MethodRecords[d.obj].Push(stored);

			// Test annotations on an in-class method collect here, with its record.
			if (auto test = ctx.model->MutableTests().Collect(fn, *stored, d.unitId); !test.Ok())
				builder.ErrorAt(ctx, &fn, test.message);

			if (classScope)
				classScope->Register(name, SymbolInfo{ SymbolKind::Function }, stored);

			builder.m_PendingMethodDecls.Remove(i);
			changed = true;
		}

		return changed;
	}

	// A template specialization `node` names in a type position, and those its children name: instantiated (no-op for
	// one that is not a template). Every node of every body passes here, so the node's kind is read, not cast.
	static void InstantiateNamedTypes(ProgramModel& model, const AstNode* node, uint32 unit)
	{
		if (!node) return;
		if (node->Type() == NodeType::QualType)
			if (const auto* qt = static_cast<const QualTypeNode*>(node); qt->HasTemplateArgs()) model.ResolveType(*qt, unit);
		for (const auto& child : node->Children()) InstantiateNamedTypes(model, child.Get(), unit);
	}

	// --- GAP A: the ONE body-opening pass (see ModelResolver.h) ------------------------------
	void ModelResolver::InstantiateBodyReferences(ModelBuilder& builder, ModelBuilder::Context& ctx)
	{
		Core::Vector<const FunctionRecord*> recs;
		ctx.model->AllFunctionRecords(recs);
		// A template's instances share its body, and walking a body again in the same unit resolves the same names.
		std::set<std::pair<const AstNode*, uint32>> walked;
		for (const auto& r : recs)
			if (r->bodyNode && r->bodyNode->Type() == NodeType::CodeBlock && walked.emplace(r->bodyNode, r->definingUnit).second)
				InstantiateNamedTypes(*ctx.model, r->bodyNode, r->definingUnit);
	}

} // namespace Scripting



