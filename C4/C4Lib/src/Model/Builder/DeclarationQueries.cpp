#include "Model/Builder/DeclarationQueries.h"
#include "Model/Builder/FreeFunctionResolver.h"
#include "Model/Builder/ModelResolver.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"

namespace Scripting
{
	DeclarationQueries::DeclarationQueries(ModelBuilder& builder, ModelBuilder::Context& ctx) : m_Builder(builder), m_Ctx(ctx)
	{
		Index();
		m_Ctx.model->m_Pending = this;
	}

	DeclarationQueries::~DeclarationQueries()
	{
		m_Ctx.model->m_Pending = nullptr;
	}

	// The lists do not grow after pass 1, so a declaration is known by its place in its list.
	void DeclarationQueries::Index()
	{
		for (uint32 i = 0; i < m_Builder.m_PendingAliases.Count(); ++i) m_Aliases.emplace(m_Builder.m_PendingAliases[i].raw, i);
		for (uint32 i = 0; i < m_Builder.m_PendingEnums.Count(); ++i) m_Enums.emplace(m_Builder.m_PendingEnums[i].raw, i);
		for (uint32 i = 0; i < m_Builder.m_PendingUnions.Count(); ++i) m_Unions.emplace(m_Builder.m_PendingUnions[i].raw, i);
		for (uint32 i = 0; i < m_Builder.m_PendingVars.Count(); ++i) m_Variables.emplace(m_Builder.m_PendingVars[i].info, i);
		for (uint32 i = 0; i < m_Builder.m_PendingFields.Count(); ++i)
		{
			const ModelBuilder::PendingField& field = m_Builder.m_PendingFields[i];
			m_Objects[field.obj].fields.Push(i);
			const Scope* members = field.var->IsStatic() ? m_Ctx.model->TypeScope(field.obj) : nullptr;
			if (!members) continue;
			for (const auto& declarator : field.var->GetDeclarators())
				for (const auto& entry : members->Entries(declarator.m_NameToken.GetText()))
					if (entry->declNode == field.var) m_StaticMembers.emplace(entry.get(), i);
		}
		for (uint32 i = 0; i < m_Builder.m_PendingSwizzles.Count(); ++i) m_Objects[m_Builder.m_PendingSwizzles[i].obj].swizzles.Push(i);
		for (uint32 i = 0; i < m_Builder.m_PendingBases.Count(); ++i) m_Objects[m_Builder.m_PendingBases[i].obj].bases.Push(i);
		for (uint32 i = 0; i < m_Builder.m_PendingMethodDecls.Count(); ++i) m_Objects[m_Builder.m_PendingMethodDecls[i].obj].methods.Push(i);
	}

	// --- Asking ------------------------------------------------------------------------------------------

	template <typename Resolve>
	QueryResult<void> DeclarationQueries::Ask(Resolution& resolution, const Link& link, Resolve resolve)
	{
		switch (resolution.stage)
		{
			case Stage::Resolved: return QueryResult<void>::Success();
			case Stage::Failed: return FailureOf(resolution, link);
			case Stage::Resolving: return DependsOnItself(link);
			case Stage::Pending: break;
		}
		resolution.stage = Stage::Resolving;
		m_Resolving.Push(link);
		QueryResult<void> result = resolve();
		m_Resolving.Remove(m_Resolving.Count() - 1);
		if (result.Ok())
		{
			resolution.stage = Stage::Resolved;
			return result;
		}
		resolution.stage = Stage::Failed;
		resolution.failure = result;
		// The resolver may have put its own words before the cycle ("the array size 'N' is not ..."): those are for
		// the declaration's own report. What asks for it, now or later, is told the cycle alone.
		if (result.error == QueryError::CyclicDependency)
			resolution.cycle = !m_LastCycle.empty() && result.message.find(m_LastCycle) != Core::String::npos ? m_LastCycle : result.message;
		return FailureOf(resolution, link);
	}

	// `the alias 'A' depends on itself: A -> B -> A`: the chain from where `link` is resolving to here.
	QueryResult<void> DeclarationQueries::DependsOnItself(const Link& link)
	{
		Core::String chain;
		bool inCycle = false;
		for (const Link& step : m_Resolving)
		{
			inCycle = inCycle || step.key == link.key;
			if (inCycle) chain += step.name + " -> ";
		}
		m_LastCycle = Core::String(link.what) + " '" + link.name + "' depends on itself: " + chain + link.name;
		return QueryResult<void>::Fail(QueryError::CyclicDependency, m_LastCycle);
	}

	// What asks for a declaration that failed is told so; the declaration's own error says why, once.
	QueryResult<void> DeclarationQueries::FailureOf(const Resolution& resolution, const Link& link)
	{
		if (resolution.cycle.empty())
			return QueryResult<void>::Fail(QueryError::NotFound, Core::String(link.what) + " '" + link.name + "' does not resolve");
		m_LastCycle = resolution.cycle;
		return QueryResult<void>::Fail(QueryError::CyclicDependency, resolution.cycle);
	}

	DeclarationQueries::Step::Step(DeclarationQueries* queries, const void* key, const Core::String& name) : m_Queries(queries)
	{
		if (m_Queries) m_Queries->m_Resolving.Push({ key, "the constant", name });
	}

	DeclarationQueries::Step::~Step()
	{
		if (m_Queries) m_Queries->m_Resolving.Remove(m_Queries->m_Resolving.Count() - 1);
	}

	// --- The queries -------------------------------------------------------------------------------------

	QueryResult<void> DeclarationQueries::Alias(const AliasType& alias)
	{
		auto found = m_Aliases.find(&alias);
		if (found == m_Aliases.end()) return QueryResult<void>::Success();
		ModelBuilder::PendingAlias& pending = m_Builder.m_PendingAliases[found->second];
		return Ask(pending.resolution, { &alias, "the alias", alias.GetName() }, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveAlias(pending); });
	}

	QueryResult<void> DeclarationQueries::Enum(const EnumType& type)
	{
		auto found = m_Enums.find(&type);
		if (found == m_Enums.end()) return QueryResult<void>::Success();
		ModelBuilder::PendingEnum& pending = m_Builder.m_PendingEnums[found->second];
		return Ask(pending.resolution, { &type, "the enum", type.GetName() }, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveEnum(pending); });
	}

	QueryResult<void> DeclarationQueries::Union(const UnionType& type)
	{
		auto found = m_Unions.find(&type);
		if (found == m_Unions.end()) return QueryResult<void>::Success();
		ModelBuilder::PendingUnion& pending = m_Builder.m_PendingUnions[found->second];
		return Ask(pending.resolution, { &type, "the union", type.GetName() }, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveUnion(pending); });
	}

	QueryResult<void> DeclarationQueries::Variable(const SymbolInfo& variable)
	{
		if (auto member = m_StaticMembers.find(&variable); member != m_StaticMembers.end()) return Field(member->second);
		auto found = m_Variables.find(&variable);
		if (found == m_Variables.end()) return QueryResult<void>::Success();
		ModelBuilder::PendingVar& pending = m_Builder.m_PendingVars[found->second];
		return Ask(pending.resolution, { &variable, "the type of", variable.plainName }, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveVariable(pending); });
	}

	QueryResult<void> DeclarationQueries::Field(uint32 index)
	{
		ModelBuilder::PendingField& pending = m_Builder.m_PendingFields[index];
		const Link link{ &pending, "the type of", pending.obj->GetName() + "::" + pending.var->GetNameToken().GetText() };
		return Ask(pending.resolution, link, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveFieldType(pending); });
	}

	QueryResult<void> DeclarationQueries::Parts(const ObjectType& object)
	{
		auto found = m_Objects.find(&object);
		if (found == m_Objects.end()) return QueryResult<void>::Success();
		ObjectWork& work = found->second;
		return Ask(work.parts, { &work.parts, "the layout of", object.GetName() }, [&] { return ResolveParts(work); });
	}

	QueryResult<void> DeclarationQueries::Methods(const ObjectType& object)
	{
		auto found = m_Objects.find(&object);
		if (found == m_Objects.end()) return QueryResult<void>::Success();
		ObjectWork& work = found->second;
		return Ask(work.declaredMethods, { &work.declaredMethods, "the methods of", object.GetName() }, [&] { return ResolveMethods(work); });
	}

	// Every part is tried, so each one that does not resolve is reported; the fields that do join the object in
	// declaration order, whatever was asked for first.
	QueryResult<void> DeclarationQueries::ResolveParts(ObjectWork& work)
	{
		ModelResolver resolver(m_Builder, m_Ctx, *this);
		QueryResult<void> first = QueryResult<void>::Success();
		auto note = [&](const QueryResult<void>& part) { if (first.Ok() && !part.Ok()) first = part; };
		for (uint32 index : work.fields)
		{
			const QueryResult<void> typed = Field(index);
			note(typed);
			if (typed.Ok()) resolver.AttachField(m_Builder.m_PendingFields[index]);
		}
		resolver.ResolveLaneViews(work.swizzles);
		for (uint32 index : work.swizzles) note(m_Builder.m_PendingSwizzles[index].resolution.failure);
		for (uint32 index : work.bases)
		{
			ModelBuilder::PendingBase& base = m_Builder.m_PendingBases[index];
			note(Ask(base.resolution, { &base, "the base of", base.obj->GetName() }, [&] { return resolver.ResolveBase(base); }));
		}
		return first;
	}

	QueryResult<void> DeclarationQueries::Method(uint32 index)
	{
		ModelBuilder::PendingMethodDecl& method = m_Builder.m_PendingMethodDecls[index];
		return Ask(method.resolution, { &method, "a method of", method.obj->GetName() }, [&] { return ModelResolver(m_Builder, m_Ctx, *this).ResolveMethod(method); });
	}

	QueryResult<void> DeclarationQueries::ResolveMethods(ObjectWork& work)
	{
		QueryResult<void> first = QueryResult<void>::Success();
		for (uint32 index : work.methods)
			if (const QueryResult<void> declared = Method(index); first.Ok() && !declared.Ok()) first = declared;
		return first;
	}

	// --- Completeness ------------------------------------------------------------------------------------

	QueryResult<void> DeclarationQueries::Complete(const Type* type)
	{
		std::unordered_set<const Type*> visited;
		return CompleteValue(type, visited);
	}

	// What the value holds is completed first, then the value is laid out again: a layout made while a member was
	// still a shell measured the shell.
	QueryResult<void> DeclarationQueries::CompleteValue(const Type* type, std::unordered_set<const Type*>& visited)
	{
		const Type* value = TypeHandle::UnwrapToValue(type);
		if (!value || !visited.insert(value).second) return QueryResult<void>::Success();
		if (const auto* array = dynamic_cast<const ArrayType*>(value)) return CompleteValue(array->GetElementType(), visited);
		if (const auto* enumeration = dynamic_cast<const EnumType*>(value)) return Enum(*enumeration);
		if (const auto* held = dynamic_cast<const UnionType*>(value))
		{
			if (auto fields = Union(*held); !fields.Ok()) return fields;
			for (const ObjectType::FieldInfo& field : held->GetFields())
				if (auto part = CompleteValue(field.m_Type, visited); !part.Ok()) return part;
			const_cast<UnionType*>(held)->CalculateLayout();
			return QueryResult<void>::Success();
		}
		const auto* object = dynamic_cast<const ObjectType*>(value);
		if (!object) return QueryResult<void>::Success();
		if (auto parts = Parts(*object); !parts.Ok()) return parts;
		for (const ObjectType* base : object->GetBaseTypes())
			if (auto part = CompleteValue(base, visited); !part.Ok()) return part;
		for (const ObjectType::FieldInfo& field : object->GetFields())
			if (!field.m_IsStatic)
				if (auto part = CompleteValue(field.m_Type, visited); !part.Ok()) return part;
		if (object->IsDefined()) const_cast<ObjectType*>(object)->CalculateLayout();
		return QueryResult<void>::Success();
	}

	// --- The sweep ---------------------------------------------------------------------------------------

	void DeclarationQueries::ResolveAll()
	{
		for (auto& variable : m_Builder.m_PendingVars) (void)Variable(*variable.info);
		for (auto& enumeration : m_Builder.m_PendingEnums) (void)Enum(*enumeration.raw);
		for (auto& alias : m_Builder.m_PendingAliases) (void)Alias(*alias.raw);
		for (auto& held : m_Builder.m_PendingUnions) (void)Union(*held.raw);
		for (auto& field : m_Builder.m_PendingFields) (void)Parts(*field.obj);
		for (auto& view : m_Builder.m_PendingSwizzles) (void)Parts(*view.obj);
		for (auto& base : m_Builder.m_PendingBases) (void)Parts(*base.obj);
		for (uint32 i = 0; i < m_Builder.m_PendingMethodDecls.Count(); ++i) (void)Method(i);
		FreeFunctionResolver functions(m_Builder, m_Ctx, *this);
		for (auto& function : m_Builder.m_PendingFuncs)
			(void)Ask(function.resolution, { &function, "the function", function.name }, [&] { return functions.ResolveFree(function); });
		for (auto& function : m_Builder.m_PendingQualifiedFuncs)
			(void)Ask(function.resolution, { &function, "the function", function.prefix + "::" + function.last }, [&] { return functions.ResolveQualified(function); });
	}
} // namespace Scripting
