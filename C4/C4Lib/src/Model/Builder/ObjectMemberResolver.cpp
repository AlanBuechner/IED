#include "Model/Builder/ModelResolver.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/SwizzleType.h"
#include "Model/OperatorAnnotations.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/SwizzleViewNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace C4Compiler
{
	// The members of ModelResolver that resolve what an object declares in its body.

	namespace
	{
		template <typename T>
		QueryResult<void> Failure(const QueryResult<T>& failed)
		{
			return QueryResult<void>::Fail(failed.error, failed.message, failed.loc);
		}

		// A `packed T[N] data` clause is the region's array view: its element is the lane type, N the width.
		const Type* LaneOf(const Type* viewType)
		{
			const Type* value = TypeHandle::UnwrapToValue(viewType);
			const auto* array = dynamic_cast<const ArrayType*>(value);
			return array ? TypeHandle::UnwrapToValue(array->GetElementType()) : value;
		}
	}

	Scope* ModelResolver::ClassScope(const ObjectType* object) const
	{
		return const_cast<Scope*>(m_Ctx.model->TypeScope(object));
	}

	// --- Fields ----------------------------------------------------------------------------------------

	// In the class scope, so nested types and members are visible. A static member's symbols were registered in
	// pass 1 (ModelBuilder::RegisterStaticMember): they take the type here, whoever asked for it.
	QueryResult<void> ModelResolver::ResolveFieldType(ModelBuilder::PendingField& f)
	{
		Scope* classScope = ClassScope(f.obj);
		auto type = m_Ctx.model->ResolveTypeInContext(*f.var->GetType(), f.unitId, classScope);
		if (!type.Ok()) return Failure(type);
		if (!type.value) return QueryResult<void>::Fail(QueryError::NotAValue, "the type has no value");
		if (f.var->IsThreadLocal() && !f.var->IsStatic())
			m_Builder.ErrorAt(m_Ctx, f.var, "field '" + f.var->GetNameToken().GetText() + "' cannot be thread_local: an object's fields live in the object; "
				"write 'static thread_local'");
		f.type = type.value;
		if (!f.var->IsStatic() || !classScope) return QueryResult<void>::Success();
		for (const auto& declarator : f.var->GetDeclarators())
			for (const auto& entry : classScope->Entries(declarator.m_NameToken.GetText()))
				if (entry->declNode == f.var) entry->type = type.value;
		return QueryResult<void>::Success();
	}

	// A comma list (`int32 x, y;`) declares several fields of the same type.
	void ModelResolver::AttachField(const ModelBuilder::PendingField& f)
	{
		ObjectType* object = const_cast<ObjectType*>(f.obj);
		for (const auto& declarator : f.var->GetDeclarators())
		{
			ObjectType::FieldInfo field;
			field.mName = declarator.m_NameToken.GetText();
			field.m_Type = f.type;
			field.m_IsStatic = f.var->IsStatic();
			field.m_IsConst = f.var->IsConst();
			field.m_IsVolatile = f.var->IsVolatile();
			field.m_Access = f.var->GetAccessModifier();
			field.m_DeclaringNode = f.var;
			if (!field.m_IsStatic) field.m_Initializer = declarator.m_Initializer.Get();
			// A lane of an anonymous union carries the region and view markers, so the layout groups them into one
			// byte range: a plain lane has view -1 (it aliases offset 0), a struct view's lanes share a view.
			field.m_InUnionRegion = f.unionRegionId >= 0;
			field.m_UnionRegionId = f.unionRegionId;
			field.m_UnionViewId = f.unionViewId;
			object->AddField(field);
		}
		object->AttachPendingPart();
	}

	// --- Lane views --------------------------------------------------------------------------------------

	QueryResult<const Type*> ModelResolver::LaneTypeOf(const ModelBuilder::PendingSwizzle& view) const
	{
		auto type = m_Ctx.model->ResolveTypeInContext(*view.node->GetElementType().Get(), view.unitId, nullptr);
		if (type.Ok() && !type.value) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "the lane type has no value");
		return type;
	}

	// The packed views of one anonymous union alias the same SIMD region (x == r when both name lane 0), so the
	// consecutive views of one lane type are one region: `union { packed T x, y; packed T r, g; }` is one
	// <2 x T> vector, which keeps the object a pure packed vector.
	uint32 ModelResolver::LastViewOfRegion(const Core::Vector<uint32>& places, uint32 first, const Type* lane) const
	{
		const auto& views = m_Builder.m_PendingSwizzles;
		uint32 last = first;
		while (last + 1 < places.Count() && places[last + 1] == places[last] + 1)
		{
			const ModelBuilder::PendingSwizzle& next = views[places[last + 1]];
			if (!next.node->IsPacked() || !views[places[first]].node->IsPacked()) break;
			auto type = LaneTypeOf(next);
			if (!type.Ok() || LaneOf(type.value) != lane) break;
			++last;
		}
		return last;
	}

	void ModelResolver::ResolveLaneViews(const Core::Vector<uint32>& places)
	{
		auto& views = m_Builder.m_PendingSwizzles;
		for (uint32 i = 0; i < places.Count(); )
		{
			ModelBuilder::PendingSwizzle& view = views[places[i]];
			auto type = LaneTypeOf(view);
			if (!type.Ok())
			{
				view.resolution.stage = ModelBuilder::Stage::Failed;
				view.resolution.failure = Failure(type);
				++i;
				continue;
			}
			const Type* lane = LaneOf(type.value);
			const uint32 last = LastViewOfRegion(places, i, lane);
			AddRegion(places, i, last, lane);
			for (uint32 k = i; k <= last; ++k)
			{
				views[places[k]].resolution.stage = ModelBuilder::Stage::Resolved;
				const_cast<ObjectType*>(view.obj)->AttachPendingPart();
			}
			i = last + 1;
		}
	}

	void ModelResolver::AddRegion(const Core::Vector<uint32>& places, uint32 first, uint32 last, const Type* lane)
	{
		const auto& views = m_Builder.m_PendingSwizzles;
		const ModelBuilder::PendingSwizzle& leading = views[places[first]];
		SwizzleType* region = new SwizzleType("packed", lane);
		region->SetRegionPacked(leading.node->IsPacked());
		for (uint32 k = first; k <= last; ++k)
		{
			const SwizzleViewNode& view = *views[places[k]].node;
			auto viewType = LaneTypeOf(views[places[k]]);
			if (const auto* array = dynamic_cast<const ArrayType*>(TypeHandle::UnwrapToValue(viewType.value)))
			{
				region->SetVectorWidth(static_cast<uint32>(array->GetElementCount()));
				if (!view.GetLanes().Empty()) region->SetArrayAlias(view.GetLanes()[0].GetText());
				continue;
			}
			uint32 index = 0; // per clause: x, y -> 0, 1 and r, g -> 0, 1 (they alias)
			for (const auto& name : view.GetLanes()) region->AddLaneAt(name.GetText(), view.IsPacked(), index++);
		}
		region->CalculateLayout();
		if (region->IsPacked())
			if (Core::String why = SwizzleType::WhyNotAVector(lane, region->GetLaneCount(), 1); !why.empty())
				m_Builder.ErrorAt(m_Ctx, leading.node, why);
		const SwizzleType* owned = m_Ctx.model->m_Types->Own<SwizzleType>(region);
		ObjectType* object = const_cast<ObjectType*>(leading.obj);
		if (owned->IsPacked())
		{
			object->AddPackedRegion(owned, leading.node, leading.access);
			return;
		}
		// An unpacked view is a struct of fields, not a vector: each lane is a field at its byte offset, so one lane's
		// access and aggregate initialization go through the ordinary field machinery.
		object->AddPackedView(owned, owned->GetName());
		const size_t laneSize = lane ? lane->GetSize() : 0;
		for (const auto& laneInfo : owned->GetLanes())
		{
			ObjectType::FieldInfo field;
			field.mName = laneInfo.m_Name;
			field.m_Type = lane;
			field.m_Offset = static_cast<size_t>(laneInfo.m_Index) * laneSize;
			object->AddField(field);
		}
		object->CalculateLayout();
	}

	// --- Bases -------------------------------------------------------------------------------------------

	QueryResult<void> ModelResolver::AttachBase(const ObjectType& derived, const Type* base)
	{
		auto checked = ClassHierarchy::CheckCanInherit(&derived, base);
		if (checked.Ok()) const_cast<ObjectType&>(derived).AddBaseType(static_cast<const ObjectType*>(base));
		return checked;
	}

	// In the declaring scope: `struct Circle : Shape<float>` inside a namespace names a sibling of that namespace. A
	// base that cannot be inherited from is an error at the base, and the declaration is resolved all the same.
	QueryResult<void> ModelResolver::ResolveBase(ModelBuilder::PendingBase& b)
	{
		auto base = m_Ctx.model->ResolveTypeInContext(*b.type, b.unitId, b.declScope);
		if (!base.Ok()) return Failure(base);
		if (auto attached = AttachBase(*b.obj, TypeHandle::UnwrapToValue(base.value)); !attached.Ok())
			m_Builder.ErrorAt(m_Ctx, b.type, attached.message);
		const_cast<ObjectType*>(b.obj)->AttachPendingPart();
		return QueryResult<void>::Success();
	}

	// --- In-class method declarations --------------------------------------------------------------------

	QueryResult<FunctionType::ParameterInfo> ModelResolver::MethodParameter(const ModelBuilder::PendingMethodDecl& d, uint32 index, Scope* classScope) const
	{
		const auto& parameter = d.fn->GetParameters()[index];
		auto type = m_Ctx.model->ResolveTypeInContext(*parameter->GetType(), d.unitId, classScope);
		if (!type.Ok()) return QueryResult<FunctionType::ParameterInfo>::Fail(type.error, type.message, type.loc);
		FunctionType::ParameterInfo info;
		info.mName = parameter->HasName() ? parameter->GetNameToken().GetText() : ("arg" + std::to_string(index));
		info.m_Type = type.value;
		return QueryResult<FunctionType::ParameterInfo>::Success(info);
	}

	// The signature resolves in the class scope; the record joins the object's methods and its parallel record list,
	// and the name the class scope, so unqualified member calls and `S::Method` find it. Overloads grow the set.
	QueryResult<void> ModelResolver::ResolveMethod(ModelBuilder::PendingMethodDecl& d)
	{
		const FunctionDefinitionNode& fn = *d.fn;
		ProgramModel& model = *m_Ctx.model;
		Scope* classScope = ClassScope(d.obj);
		auto returned = model.ResolveTypeInContext(*fn.GetReturnType(), d.unitId, classScope);
		if (!returned.Ok()) return Failure(returned);
		Core::Vector<FunctionType::ParameterInfo> params;
		uint32 trailingDefaults = 0;
		for (uint32 i = 0; i < fn.GetParameters().Count(); ++i)
		{
			auto parameter = MethodParameter(d, i, classScope);
			if (!parameter.Ok()) return Failure(parameter);
			params.Push(parameter.value);
			if (fn.GetParameters()[i]->HasDefaultValue()) trailingDefaults++;
		}

		const Core::String name = fn.GetName()->GetFirstIdentifierToken().GetText();
		Core::Vector<const Type*> paramTypes;
		for (const auto& p : params) paramTypes.Push(p.m_Type);
		const FunctionType* signature = model.m_Types->Function(returned.value, paramTypes, fn.IsVariadic()); // interned

		FunctionRecord rec;
		rec.type = signature;
		rec.scope = classScope;
		rec.containingType = d.obj;
		rec.bodyNode = fn.GetBody().Get();
		rec.declarations.Push(&fn);
		rec.definingUnit = d.unitId;
		rec.declUnit = d.unitId; // declared here: the visibility domain (stable)
		rec.isConst = fn.IsConstMethod();
		rec.mangledName = MethodSymbol(classScope, *d.obj, name, *signature, rec.isConst);
		rec.name = name;
		for (const auto& annotation : fn.GetAnnotations()) CollectGroupAnnotations(annotation.Get(), rec.groups);
		rec.minArity = params.Count() > trailingDefaults ? params.Count() - trailingDefaults : 0;
		for (const auto& p : params) rec.paramNames.Push(p.mName); // names for the lowerer (FunctionType drops them)
		for (const auto& p : fn.GetParameters()) rec.paramDefaults.Push(p->GetDefaultValue().Get());
		rec.paramIsConst = fn.ParameterConstness();
		rec.isStatic = fn.IsStatic();
		rec.isVirtual = fn.IsVirtual();
		rec.isNoexcept = fn.IsNoexcept();
		rec.access = fn.GetAccessModifier();
		for (const auto& annotation : fn.GetAnnotations())
			CollectOperatorAnnotations(annotation.Get(), fn.GetParameters().Count(), rec.operatorAnnotations);

		const FunctionRecord* stored = model.Symbols()->AddFunctionRecord(rec);
		model.m_RecordBySignature.emplace(signature, stored);

		ObjectType::MethodInfo method;
		method.mName = name;
		method.m_ReturnType = returned.value;
		method.m_ParameterTypes = paramTypes;
		method.m_IsStatic = fn.IsStatic();
		method.m_IsVirtual = fn.IsVirtual();
		method.m_IsOverride = fn.IsOverride();
		method.m_IsFinal = fn.IsFinal();
		method.m_IsConst = fn.IsConstMethod();
		method.m_Access = fn.GetAccessModifier();
		method.m_DeclaringNode = &fn;
		method.m_HasBody = fn.GetBody() != nullptr;
		method.m_OperatorAnnotations = rec.operatorAnnotations;
		const_cast<ObjectType*>(d.obj)->AddMethod(method);
		model.m_MethodRecords[d.obj].Push(stored);

		// Test annotations on an in-class method collect here, with its record.
		if (auto test = model.MutableTests().Collect(fn, *stored, d.unitId); !test.Ok())
			m_Builder.ErrorAt(m_Ctx, &fn, test.message);

		if (classScope) classScope->Register(name, SymbolInfo{ SymbolKind::Function }, stored);
		return QueryResult<void>::Success();
	}
} // namespace C4Compiler
