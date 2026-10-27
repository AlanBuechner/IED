#include "Model/Templates/ObjectTemplateBody.h"
#include "Model/Templates/PartialOrdering.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/Templates/TemplateMethodDefinitions.h"
#include "Model/Templates/NestedTypes.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/SwizzleType.h"
#include "Model/OperatorAnnotations.h"
#include "Model/TypeHandle.h"
#include "Model/ConstEvaluator.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Scripting
{
	namespace
	{
		using Result = QueryResult<void>;

		bool IsBareParameter(const AstNode* pattern, const TemplateDefinitionNode* params)
		{
			const auto* q = dynamic_cast<const QualTypeNode*>(pattern);
			if (!q || q->IndirectionCount() || q->IsLeadingConst() || q->FuncPtrType() || !q->BaseName() || q->BaseName()->SegmentCount() != 1) return false;
			const Core::String& name = q->BaseName()->GetSegments()[0].nameToken.GetText();
			for (uint32 i = 0; params && i < params->ParameterCount(); ++i)
				if (params->GetParameter(i)->GetName() == name) return true;
			return false;
		}

		Result Fail(const Core::String& what) { return Result::Fail(QueryError::NotAValue, what); }

	}

	QueryResult<void> ObjectTemplateBody::Build()
	{
		if (auto bases = AddBases(); !bases.Ok()) return bases;
		for (const auto& member : m_Def.objectBody->GetBodyMembers())
			if (auto r = AddMember(member.Get()); !r.Ok()) return r;
		AddMemberTemplateDefinitions();
		return CheckDefinitionsTaken();
	}

	QueryResult<void> ObjectTemplateBody::AddBases()
	{
		for (const auto& b : m_Def.objectBody->GetBaseClasses())
		{
			auto bt = m_Model.ResolveTypeInternal(*b.m_Type, m_UnitId, m_Binding, nullptr);
			if (!bt.Ok()) return Fail("base class: " + bt.message);
			if (auto checked = ClassHierarchy::CheckCanInherit(&m_Instance, TypeHandle::UnwrapToValue(bt.value)); !checked.Ok()) return checked;
			m_Instance.AddBaseType(static_cast<const ObjectType*>(TypeHandle::UnwrapToValue(bt.value)));
		}
		if (auto rule = ClassHierarchy::CheckBaseList(&m_Instance); !rule.Ok()) return Fail(rule.message);
		return Result::Success();
	}

	QueryResult<void> ObjectTemplateBody::AddMember(const AstNode* member)
	{
		if (const auto* al = dynamic_cast<const AliasDefinitionNode*>(member)) return AddAlias(*al);
		if (const auto* var = dynamic_cast<const VariableDefinitionNode*>(member)) return AddFields(*var);
		if (const auto* un = dynamic_cast<const UnionDefinitionNode*>(member)) return AddPackedRegion(*un);
		if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(member))
			return fn->IsTemplated() ? MemberTemplates::Declare(m_Model, &m_Instance, *fn, m_Scope, m_Binding) : AddMethod(*fn);
		return AddNestedType(member);
	}

	QueryResult<void> ObjectTemplateBody::AddNestedType(const AstNode* member)
	{
		const InstantiationBindings bindings = OwnBindings();
		const NestedTypes::Owner owner{ &m_Instance, &bindings, m_Binding, m_Scope, m_UnitId };
		QueryResult<void> added = Result::Success();
		if (const auto* nested = dynamic_cast<const ObjectDefinitionNode*>(member)) added = NestedTypes::AddObject(m_Model, owner, *nested);
		else if (const auto* en = dynamic_cast<const EnumDefinitionNode*>(member)) added = NestedTypes::AddEnum(m_Model, owner, *en);
		return added.Ok() ? added : Fail("nested type: " + added.message);
	}

	InstantiationBindings ObjectTemplateBody::OwnBindings() const
	{
		InstantiationBindings bindings = m_Def.params ? BindingsFor(m_Model, *m_Def.params, m_Args) : InstantiationBindings();
		if (!m_Def.params && m_Owner) bindings = *m_Owner;
		bindings.typeParams.Push({ m_Name, &m_Instance });
		return bindings;
	}

	QueryResult<void> ObjectTemplateBody::AddAlias(const AliasDefinitionNode& alias)
	{
		// A member alias (`alias Type = T;`) is visible to the members after it.
		auto at = m_Model.ResolveTypeInternal(*alias.GetAliasedType(), m_UnitId, m_Binding, nullptr);
		if (!at.Ok()) return Fail("alias: " + at.message);
		SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = at.value; sym.definingUnit = 0;
		m_Binding->Register(alias.GetNameToken().GetText(), sym);
		return Result::Success();
	}

	QueryResult<void> ObjectTemplateBody::AddFields(const VariableDefinitionNode& var)
	{
		auto vt = m_Model.ResolveTypeInternal(*var.GetType(), m_UnitId, m_Binding, nullptr);
		if (!vt.Ok()) return Fail("field: " + vt.message);
		if (var.IsThreadLocal() && !var.IsStatic())
			return Fail("field '" + var.GetNameToken().GetText() + "' cannot be thread_local: write 'static thread_local'");
		for (const auto& d : var.GetDeclarators()) // `T first, second;` declares one field per declarator
		{
			ObjectType::FieldInfo f;
			f.mName = d.m_NameToken.GetText();
			f.m_Type = vt.value;
			f.m_IsStatic = var.IsStatic();
			f.m_IsConst = var.IsConst();
			f.m_IsVolatile = var.IsVolatile();
			f.m_Access = var.GetAccessModifier();
			f.m_DeclaringNode = &var;
			if (!f.m_IsStatic) f.m_Initializer = d.m_Initializer.Get();
			m_Instance.AddField(f);
			if (f.m_IsStatic) AddStatic(var, d, vt.value);
		}
		return Result::Success();
	}

	void ObjectTemplateBody::AddStatic(const VariableDefinitionNode& var, const Declarator& d, const Type* type)
	{
		// The initializer stays on the declarator: the program-wide constant sweep has no bindings to fold it in.
		SymbolInfo sym;
		sym.kind = SymbolKind::Variable;
		sym.type = type;
		sym.isConst = var.IsConst();
		sym.isStatic = true;
		sym.isVolatile = var.IsVolatile();
		sym.isThreadLocal = var.IsThreadLocal();
		sym.definingUnit = 0;
		sym.declNode = &var;
		sym.plainName = d.m_NameToken.GetText();
		sym.mangledName = CanonicalSymbolName(m_Scope, sym.plainName);
		if (d.m_Initializer)
			if (auto folded = ConstEvaluator(m_Model, m_Scope).EvaluateStart(d.m_Initializer.Get(), type, m_UnitId, m_Binding); folded.Ok())
				sym.constValue = folded.value;
		m_Scope->Register(sym.plainName, sym);
	}

	QueryResult<void> ObjectTemplateBody::AddPackedRegion(const UnionDefinitionNode& un)
	{
		// An anonymous union of `packed T x, y, z;` views is one SIMD region: clauses with the same element
		// type alias the same lanes (x == r at index 0), so they merge into ONE SwizzleType. A
		// `packed T[N] v;` clause is the whole-vector array view: it gives the width and the array alias.
		if (!un.GetNameToken().GetText().empty()) return Result::Success();
		const Type* scalar = nullptr;
		uint32 width = 0;
		Core::String arrayAlias;
		Core::Vector<std::pair<const SwizzleViewNode*, const Type*>> laneViews;
		for (const auto& member : un.GetMembers())
		{
			const auto* sv = dynamic_cast<const SwizzleViewNode*>(member.Get());
			if (!sv || !sv->IsPacked()) continue;
			auto et = m_Model.ResolveTypeInternal(*sv->GetElementType().Get(), m_UnitId, m_Binding, nullptr);
			if (!et.Ok() || !et.value) return Fail("swizzle element type: " + et.message);
			const Type* elem = TypeHandle::UnwrapToValue(et.value);
			if (const auto* ar = dynamic_cast<const ArrayType*>(elem))
			{
				scalar = scalar ? scalar : ar->GetElementType();
				width = static_cast<uint32>(ar->GetElementCount());
				if (!sv->GetLanes().Empty()) arrayAlias = sv->GetLanes()[0].GetText();
				continue;
			}
			scalar = scalar ? scalar : elem;
			laneViews.Push({ sv, elem });
		}
		if (!scalar) return Result::Success();
		SwizzleType* region = new SwizzleType("packed", scalar);
		region->SetRegionPacked(true);
		for (const auto& [sv, elem] : laneViews)
		{
			uint32 index = 0; // per clause: x, y -> 0, 1 and r, g -> 0, 1 (they alias)
			for (const auto& lane : sv->GetLanes()) region->AddLaneAt(lane.GetText(), true, index++);
		}
		if (width > 0 && !arrayAlias.empty()) { region->SetVectorWidth(width); region->SetArrayAlias(arrayAlias); }
		region->CalculateLayout();
		if (Core::String why = SwizzleType::WhyNotAVector(scalar, region->GetLaneCount(), 1); !why.empty())
		{
			delete region;
			return Fail(why);
		}
		const SwizzleType* owned = m_Model.m_Types->Own<SwizzleType>(region);
		m_Instance.AddPackedRegion(owned, &un);
		return Result::Success();
	}

	QueryResult<void> ObjectTemplateBody::AddMethod(const FunctionDefinitionNode& fn)
	{
		auto rt = m_Model.ResolveTypeInternal(*fn.GetReturnType(), m_UnitId, m_Binding, nullptr);
		if (!rt.Ok()) return Fail("return type: " + rt.message);
		ObjectType::MethodInfo m;
		m.mName = fn.GetName()->GetFirstIdentifierToken().GetText();
		m.m_ReturnType = rt.value;
		Core::Vector<FunctionType::ParameterInfo> params;
		for (const auto& p : fn.GetParameters())
		{
			auto pt = m_Model.ResolveTypeInternal(*p->GetType(), m_UnitId, m_Binding, nullptr);
			if (!pt.Ok()) return Fail("parameter: " + pt.message);
			m.m_ParameterTypes.Push(pt.value);
			FunctionType::ParameterInfo pi; pi.mName = p->GetNameToken().GetText(); pi.m_Type = pt.value;
			params.Push(pi);
		}

		// A declaration without a body takes the body of its out-of-line definition, matched by name and
		// parameter types; the body's parameter names are the definition's (C++ lets them differ).
		const AstNode* body = fn.GetBody().Get();
		OutOfLine ool;
		if (!body)
		{
			ool = FindOutOfLine(m.mName, m.m_ParameterTypes, fn.IsConstMethod());
			if (ool.fn) body = ool.fn->GetBody().Get();
		}
		const FunctionDefinitionNode& named = ool.fn ? *ool.fn : fn;

		FunctionType* sig = new FunctionType(rt.value, params, fn.IsVariadic());
		FunctionRecord rec;
		rec.type = m_Model.m_Types->Own(sig);
		rec.scope = m_Scope;
		rec.containingType = &m_Instance;
		rec.isStatic = fn.IsStatic();
		rec.isVirtual = fn.IsVirtual();
		rec.isNoexcept = fn.IsNoexcept();
		rec.isConst = fn.IsConstMethod();
		rec.access = fn.GetAccessModifier();
		rec.templateOrigin = TemplateOrigin::ClassMember;
		rec.bodyNode = body;
		rec.declarations.Push(&fn);
		if (ool.fn) rec.declarations.Push(ool.fn);
		rec.definingUnit = 0;
		rec.mangledName = MethodSymbol(m_Scope, m_Instance, m.mName, *sig, rec.isConst);
		rec.name = m.mName;
		rec.minArity = params.Count() - fn.TrailingDefaultCount();
		for (const auto& p : named.GetParameters()) rec.paramNames.Push(p->HasName() ? p->GetNameToken().GetText() : "arg");
		rec.paramIsConst = named.ParameterConstness();
		for (const auto& p : fn.GetParameters()) rec.paramDefaults.Push(p->GetDefaultValue().Get());
		for (const auto& ann : fn.GetAnnotations()) CollectOperatorAnnotations(ann.Get(), fn.GetParameters().Count(), rec.operatorAnnotations);
		if (auto lifecycle = CheckLifecycleOperators(rec); !lifecycle.Ok()) return Fail(lifecycle.message);

		const FunctionRecord* stored = m_Model.m_Symbols->AddFunctionRecord(rec);
		m_Model.m_MethodRecords[&m_Instance].Push(stored);
		m_Model.m_RecordBySignature.emplace(rec.type, stored);
		if (ool.fn && ool.binding != m_Binding) RecordOwnBindings(stored, *ool.fn);

		m.m_IsStatic = fn.IsStatic();
		m.m_IsVirtual = fn.IsVirtual();
		m.m_IsConst = fn.IsConstMethod();
		m.m_IsOverride = fn.IsOverride();
		m.m_IsFinal = fn.IsFinal();
		m.m_Access = rec.access;
		m.m_DeclaringNode = &fn;
		m.m_HasBody = body != nullptr;
		m.m_OperatorAnnotations = rec.operatorAnnotations;
		m_Instance.AddMethod(m);
		return Result::Success();
	}

	ObjectTemplateBody::OutOfLine ObjectTemplateBody::FindOutOfLine(const Core::String& method, const Core::Vector<const Type*>& parameters, bool isConst) const
	{
		if (!m_Def.params) return {}; // a member type of an instance defines its methods in its body
		for (const auto& def : m_Model.m_TemplateMethodDefs)
		{
			if (def.name != method || def.arity != parameters.Count() || def.definition->IsConstMethod() != isConst) continue;
			if (def.definition->GetOwnerTemplateDefinition() || !Defines(def)) continue; // a member template's definition
			Scope* binding = BindingFor(*def.definition);
			if (SameParameters(*def.definition, binding, parameters)) return { def.definition, binding };
		}
		return {};
	}

	// The definition is written for this template (not a same-named one elsewhere) and its qualifier names this
	// definition: `Box<T>::` the primary, `Box<T*>::` that specialization.
	bool ObjectTemplateBody::Defines(const ProgramModel::TemplateMethodDef& def) const
	{
		return def.owner == m_Name && TemplateMethodDefinitions::OwnerScope(m_Model, def) == m_Def.declScope && QualifierFits(*def.definition);
	}

	bool ObjectTemplateBody::QualifierFits(const FunctionDefinitionNode& definition) const
	{
		// `Box<T>::Get` defines the primary template's method, `Box<T*>::Get` that specialization's.
		const auto& segments = definition.GetName()->GetSegments();
		const TemplateArgumentListNode* owner = segments.Count() >= 2 ? segments[segments.Count() - 2].templateArgs.Get() : nullptr;
		const TemplatePatterns written = TemplatePatterns::OfArguments(owner, ClassParameters(definition));
		if (!m_Def.pattern)
		{
			// The primary's own parameters, by any names (`Tuple<Us...>::`): no argument is a pattern.
			for (const AstNode* p : written.patterns)
				if (!IsBareParameter(p, written.params)) return false;
			return !written.expansion || IsBareParameter(written.expansion, written.params);
		}
		const TemplatePatterns specialization = TemplatePatterns::OfArguments(m_Def.pattern, m_Def.params);
		return PartialOrdering::AtLeastAsSpecialized(written, specialization) && PartialOrdering::AtLeastAsSpecialized(specialization, written);
	}

	Scope* ObjectTemplateBody::BindingFor(const FunctionDefinitionNode& definition) const
	{
		// The definition's own template parameter names, bound to this instance's arguments by position.
		const TemplateDefinitionNode* own = ClassParameters(definition);
		const TemplateDefinitionNode* declared = m_Def.params;
		if (!own || !declared || own->ParameterCount() != declared->ParameterCount()) return m_Binding;
		bool sameNames = true;
		for (uint32 i = 0; i < own->ParameterCount(); ++i)
			sameNames = sameNames && own->GetParameter(i)->GetName() == declared->GetParameter(i)->GetName();
		if (sameNames) return m_Binding;
		Scope* binding = m_Model.m_Symbols->CreateScope(Scope::Kind::Block, "ool", m_Binding);
		TemplateArgumentResolver::BindAll(m_Model, binding, *own, m_Args);
		return binding;
	}

	bool ObjectTemplateBody::SameParameters(const FunctionDefinitionNode& definition, Scope* binding, const Core::Vector<const Type*>& parameters) const
	{
		for (uint32 i = 0; i < parameters.Count(); ++i)
		{
			auto t = m_Model.ResolveTypeInternal(*definition.GetParameters()[i]->GetType(), m_UnitId, binding, nullptr);
			if (!t.Ok() || t.value != parameters[i]) return false;
		}
		return true;
	}

	const TemplateDefinitionNode* ObjectTemplateBody::ClassParameters(const FunctionDefinitionNode& definition)
	{
		const auto& owner = definition.GetOwnerTemplateDefinition();
		return owner ? owner.Get() : definition.GetTemplateDefinition().Get();
	}

	void ObjectTemplateBody::AddMemberTemplateDefinitions() const
	{
		if (!m_Def.params) return;
		for (const auto& def : m_Model.m_TemplateMethodDefs)
		{
			if (!def.definition->GetOwnerTemplateDefinition() || !MemberTemplates::Declares(m_Model, &m_Instance, def.name) || !Defines(def)) continue;
			Scope* binding = BindingFor(*def.definition);
			InstantiationBindings names;
			if (binding != m_Binding) names = BindingsFor(m_Model, *ClassParameters(*def.definition), m_Args);
			MemberTemplates::Define(m_Model, &m_Instance, *def.definition, binding, names);
		}
	}

	QueryResult<void> ObjectTemplateBody::CheckDefinitionsTaken() const
	{
		if (!m_Def.params) return Result::Success();
		for (const auto& def : m_Model.m_TemplateMethodDefs)
		{
			if (def.definition->GetOwnerTemplateDefinition() || !Defines(def) || Takes(*def.definition)) continue;
			return Fail("the definition of '" + def.owner + "::" + def.name + "' on line " + std::to_string(def.definition->startLocation.line) +
				" matches no method '" + def.owner + "' declares without a body");
		}
		return Result::Success();
	}

	bool ObjectTemplateBody::Takes(const FunctionDefinitionNode& definition) const
	{
		auto records = m_Model.m_MethodRecords.find(&m_Instance);
		if (records == m_Model.m_MethodRecords.end()) return false;
		for (const FunctionRecord* rec : records->second)
			for (const AstNode* declaration : rec->declarations)
				if (declaration == &definition) return true;
		return false;
	}

	void ObjectTemplateBody::RecordOwnBindings(const FunctionRecord* rec, const FunctionDefinitionNode& definition) const
	{
		const TemplateDefinitionNode* own = definition.GetTemplateDefinition().Get();
		if (!own) return;
		m_Model.Writes().Ask("a method's bindings");
		m_Model.m_FuncInstantiationBindings.emplace(rec, BindingsFor(m_Model, *own, m_Args));
	}
} // namespace Scripting
