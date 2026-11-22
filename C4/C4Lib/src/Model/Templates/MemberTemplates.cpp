#include "Model/Templates/MemberTemplates.h"
#include "Model/Types/ClassHierarchy.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"

namespace C4Compiler
{
	Core::String MemberTemplates::NameOf(const FunctionDefinitionNode& fn)
	{
		return fn.GetName()->GetSegments().Back().nameToken.GetText();
	}

	QueryResult<void> MemberTemplates::Declare(const ProgramModel& model, const ObjectType* owner, const FunctionDefinitionNode& fn, const Site& site)
	{
		const Core::String name = NameOf(fn);
		if (fn.GetTemplateDefinition()->ParameterCount() == 0)
			return QueryResult<void>::Fail(QueryError::NotAValue, "'template<>' in '" + owner->GetName() + "' specializes '" + name +
				"': write the specialization outside the class");
		if (fn.IsVirtual())
			return QueryResult<void>::Fail(QueryError::NotAValue, "the member function template '" + owner->GetName() + "::" + name + "' cannot be virtual");
		ProgramModel::TemplateDef def;
		def.params = fn.GetTemplateDefinition().Get();
		def.functionBody = &fn;
		def.declScope = site.scope;
		def.memberOf = owner;
		def.bindingParent = site.binding;
		def.unit = site.unit;
		model.Making().AssertMaking("a member template");
		model.m_MemberTemplates[owner].push_back(def);
		return QueryResult<void>::Success();
	}

	void MemberTemplates::Define(const ProgramModel& model, const ObjectType* owner, const FunctionDefinitionNode& definition, Scope* binding,
		const InstantiationBindings& ownerNames, uint32 unit)
	{
		ProgramModel::TemplateDef def;
		def.params = definition.GetTemplateDefinition().Get();
		def.functionBody = &definition;
		def.declScope = model.TypeScope(owner);
		def.memberOf = owner;
		def.bindingParent = binding;
		def.ownerNames = ownerNames;
		def.unit = unit;
		model.Making().AssertMaking("a member template");
		model.m_MemberTemplates[owner].push_back(def);
	}

	void MemberTemplates::AttachDefinitions(const ProgramModel& model)
	{
		// A plain class's member template is defined with one header; a class template's with two, attached when
		// its instances are made.
		std::vector<const ObjectType*> owners;
		for (const auto& entry : model.m_MemberTemplates) owners.push_back(entry.first);
		for (const auto& def : model.m_TemplateMethodDefs)
		{
			if (def.definition->GetOwnerTemplateDefinition()) continue;
			for (const ObjectType* owner : owners)
				if (owner->GetName() == def.owner && Declares(model, owner, def.name)) Define(model, owner, *def.definition, nullptr, {}, def.unitId);
		}
	}

	bool MemberTemplates::Declares(const ProgramModel& model, const ObjectType* owner, const Core::String& name)
	{
		auto it = model.m_MemberTemplates.find(owner);
		if (it == model.m_MemberTemplates.end()) return false;
		for (const ProgramModel::TemplateDef& def : it->second)
			if (NameOf(*def.functionBody) == name) return true;
		return false;
	}

	Core::Vector<const ProgramModel::TemplateDef*> MemberTemplates::Named(const ProgramModel& model, const ObjectType* type, const Core::String& name)
	{
		Core::Vector<const ProgramModel::TemplateDef*> out;
		if (model.m_MemberTemplates.empty()) return out;
		ClassHierarchy::ForEachSubobject(type, [&](const ObjectType* t, int64) {
			auto it = model.m_MemberTemplates.find(t);
			if (it == model.m_MemberTemplates.end()) return;
			for (const ProgramModel::TemplateDef& def : it->second)
				if (NameOf(*def.functionBody) == name && !out.Contains(&def)) out.Push(&def);
		});
		return out;
	}
} // namespace C4Compiler
