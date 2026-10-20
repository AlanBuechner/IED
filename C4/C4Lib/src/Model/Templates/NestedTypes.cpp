#include "Model/Templates/NestedTypes.h"
#include "Model/TemplateInstantiator.h"
#include "Model/ConstEvaluator.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"

namespace Scripting
{
	QueryResult<void> NestedTypes::AddObject(const ProgramModel& model, const Owner& owner, const ObjectDefinitionNode& nested)
	{
		const Core::String& name = nested.GetNameToken().GetText();
		ProgramModel::TemplateDef def;
		def.params = nested.GetTemplateDefinition().Get();
		def.objectBody = &nested;
		def.declScope = owner.scope;
		def.bindingParent = owner.binding;
		if (nested.IsTemplated())
		{
			model.m_TypeTemplates[name].push_back(def);
			return QueryResult<void>::Success();
		}
		auto type = TemplateInstantiator().InstantiateMemberType(model, def, name, { *owner.bindings, EnclosingNames(owner), owner.unitId });
		if (!type.Ok()) return QueryResult<void>::Fail(type.error, type.message);
		Register(owner, name, TypeSymbol(type.value));
		return QueryResult<void>::Success();
	}

	QueryResult<void> NestedTypes::AddEnum(const ProgramModel& model, const Owner& owner, const EnumDefinitionNode& nested)
	{
		const Core::String& name = nested.GetNameToken().GetText();
		EnumType* en = new EnumType(name, model.m_Types->Builtin(TypeKind::Int32));
		model.m_Types->Own(en);
		en->SetScoped(nested.IsScoped());
		en->SetDeclNode(&nested);
		en->SetDeclScope(owner.scope);
		en->SetEnclosingNames(EnclosingNames(owner));
		if (nested.HasUnderlyingType())
		{
			auto underlying = model.ResolveTypeInternal(*nested.GetUnderlyingType(), owner.unitId, owner.binding, nullptr);
			if (!underlying.Ok()) return QueryResult<void>::Fail(underlying.error, "the underlying type of '" + name + "': " + underlying.message);
			en->SetUnderlyingType(underlying.value);
		}
		int64 next = 0;
		for (const auto& member : nested.GetMembers())
		{
			int64 value = next;
			if (member->HasInitializer())
			{
				auto folded = ConstEvaluator(model, owner.scope).Evaluate(member->GetInitializer().Get(), owner.unitId, owner.binding);
				if (!folded.Ok()) return QueryResult<void>::Fail(folded.error, "the value of '" + name + "::" + member->GetNameToken().GetText() + "': " + folded.message);
				value = folded.value.AsInt();
			}
			en->AddMember({ member->GetNameToken().GetText(), value });
			next = value + 1;
		}
		model.m_EnumTypes.Push(en);
		Register(owner, name, TypeSymbol(en));
		if (nested.IsScoped()) return QueryResult<void>::Success();
		for (const auto& member : en->GetMembers())
		{
			SymbolInfo constant;
			constant.kind = SymbolKind::Variable;
			constant.type = en;
			constant.isConst = true;
			constant.constValue = ConstantValue::Int(member.m_Value);
			constant.definingUnit = 0;
			constant.declNode = &nested;
			Register(owner, member.mName, constant);
		}
		return QueryResult<void>::Success();
	}

	// The binding layer is what the instance's own members resolve in; the class layer is what
	// `Outer<float>::Inner` and the methods' bodies find.
	void NestedTypes::Register(const Owner& owner, const Core::String& name, const SymbolInfo& symbol)
	{
		owner.binding->Register(name, symbol);
		owner.scope->Register(name, symbol);
	}

	SymbolInfo NestedTypes::TypeSymbol(const Type* type)
	{
		SymbolInfo symbol;
		symbol.kind = SymbolKind::Type;
		symbol.type = type;
		symbol.definingUnit = 0;
		return symbol;
	}

	Core::Vector<Core::String> NestedTypes::EnclosingNames(const Owner& owner)
	{
		Core::Vector<Core::String> names;
		names.Push(QualifiedName(owner.type));
		return names;
	}
} // namespace Scripting
