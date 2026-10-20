#include "Model/Templates/FunctionTemplates.h"
#include "Model/Types/SignatureType.h"
#include "Model/Templates/TemplateArguments.h"
#include "Model/Templates/TemplateDeduction.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/TemplateInstantiator.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ClassHierarchy.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include <algorithm>

namespace Scripting
{
	namespace
	{
		Core::String LastSegment(const Core::String& name)
		{
			const size_t at = name.rfind("::");
			return at == Core::String::npos ? name : name.substr(at + 2);
		}

		Core::String Qualifier(const Core::String& name)
		{
			const size_t at = name.rfind("::");
			return at == Core::String::npos ? Core::String() : name.substr(0, at);
		}

		bool IsExtension(const FunctionDefinitionNode* fn)
		{
			return fn && fn->IsExtensionMethod();
		}

		// Whether `extends` names the template `instance` is an instance of (cheap: no instantiation).
		bool ExtendsTemplateOf(const FunctionDefinitionNode& fn, const ObjectType* instance)
		{
			const auto& extended = fn.GetExtendsTypeName();
			return extended && extended->SegmentCount() > 0 && extended->GetSegments().Back().nameToken.GetText() == instance->GetName();
		}

		const ObjectType* AsObject(const Type* t)
		{
			t = TypeHandle::UnwrapToValue(t);
			return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
		}

		bool SameSignature(const FunctionType* a, const FunctionType* b)
		{
			if (a->GetReturnType() != b->GetReturnType() || a->GetParameterCount() != b->GetParameterCount()) return false;
			for (uint32 i = 0; i < a->GetParameterCount(); ++i)
				if (a->GetParameters()[i].m_Type != b->GetParameters()[i].m_Type) return false;
			return true;
		}
	}

	void FunctionTemplates::Collect(const ProgramModel& model, const TemplateCall& call, Core::Vector<OverloadCandidate>& out,
		Core::Vector<Core::String>& reasons)
	{
		CollectFrom(model, call, Visible(model, call), out, reasons);
	}

	void FunctionTemplates::CollectMembers(const ProgramModel& model, const TemplateCall& call, const ObjectType* owner,
		Core::Vector<OverloadCandidate>& out, Core::Vector<Core::String>& reasons)
	{
		CollectFrom(model, call, MemberTemplates::Named(model, owner, LastSegment(call.name)), out, reasons);
	}

	void FunctionTemplates::CollectFrom(const ProgramModel& model, const TemplateCall& call, const Core::Vector<const ProgramModel::TemplateDef*>& defs,
		Core::Vector<OverloadCandidate>& out, Core::Vector<Core::String>& reasons)
	{
		const Core::String name = LastSegment(call.name);
		for (const ProgramModel::TemplateDef* def : defs)
		{
			auto rec = Instantiate(model, *def, name, call);
			if (!rec.Ok()) { reasons.Push(rec.message); continue; }
			bool present = false;
			for (const OverloadCandidate& c : out) present = present || c.record == rec.value;
			if (!present) out.Push({ rec.value->type, rec.value, rec.value->minArity });
		}
	}

	const TemplateNameType* FunctionTemplates::NameOf(const ProgramModel& model, const TemplateCall& call)
	{
		if (Visible(model, call).Empty()) return nullptr;
		TemplateCall named = call;
		named.written = nullptr;
		named.argTypes = nullptr;
		const ProgramModel* owner = &model;
		auto* type = new TemplateNameType(LastSegment(call.name), [owner, named](const SignatureType& target) {
			return ForTarget(*owner, named, target);
		});
		model.m_Types->Own(type);
		return type;
	}

	QueryResult<const FunctionRecord*> FunctionTemplates::ForTarget(const ProgramModel& model, TemplateCall call, const SignatureType& target)
	{
		using Result = QueryResult<const FunctionRecord*>;
		const Core::Vector<const Type*> params = target.GetParameterTypes();
		call.argTypes = &params;
		call.returnType = target.GetReturnType();
		Core::Vector<OverloadCandidate> candidates;
		Core::Vector<Core::String> reasons;
		Collect(model, call, candidates, reasons);
		const FunctionRecord* found = nullptr;
		for (const OverloadCandidate& c : candidates)
		{
			Core::Vector<const Type*> own;
			for (const auto& p : c.sig->GetParameters()) own.Push(p.m_Type);
			if (!target.HasSignature(c.sig->GetReturnType(), own)) continue;
			if (found && found != c.record)
				return Result::Fail(QueryError::Ambiguous, "more than one template '" + LastSegment(call.name) + "' has an instance of the signature '" +
					target.GetName() + "'");
			found = c.record;
		}
		if (found) return found;
		return Result::Fail(QueryError::NotFound, "no instance of the template '" + LastSegment(call.name) + "' has the signature '" +
			target.GetName() + "'" + (reasons.Empty() ? Core::String() : ": " + reasons[0]));
	}

	void FunctionTemplates::AttachExtensions(const ProgramModel& model, const ObjectType* instance, uint32 unitId)
	{
		Core::Vector<const FunctionRecord*> attached;
		for (const auto& [name, defs] : model.m_FunctionTemplates)
			for (const auto& def : defs)
			{
				if (!IsExtension(def.functionBody) || !ExtendsTemplateOf(*def.functionBody, instance)) continue;
				TemplateCall call;
				call.name = name;
				call.receiver = instance;
				call.unitId = unitId;
				auto rec = Instantiate(model, def, name, call);
				if (!rec.Ok() || rec.value->containingType != instance || attached.Contains(rec.value)) continue;
				attached.Push(rec.value);
				AttachAsMethod(model, instance, rec.value, *def.functionBody);
			}
	}

	Core::Vector<const ProgramModel::TemplateDef*> FunctionTemplates::Visible(const ProgramModel& model, const TemplateCall& call)
	{
		// A member call names extension templates (any namespace: the receiver selects them). A qualified free
		// call names the templates of that namespace; an unqualified one, those of the innermost enclosing
		// scope that declares the name, else every one of that name.
		Core::Vector<const ProgramModel::TemplateDef*> out;
		const bool member = call.receiver != nullptr;
		const Core::String qualifier = Qualifier(call.name);
		const Scope* ns = nullptr;
		if (!member && !qualifier.empty())
		{
			// `P::F(1)` names P's member templates.
			const SymbolInfo* info = model.FindVisibleQualified(qualifier, call.unitId, call.scope);
			if (info && info->kind == SymbolKind::Type)
				if (const ObjectType* owner = AsObject(info->type)) return MemberTemplates::Named(model, owner, LastSegment(call.name));
			if (!info || info->kind != SymbolKind::Namespace) return out;
			ns = info->scope;
		}
		auto it = model.m_FunctionTemplates.find(LastSegment(call.name));
		if (it == model.m_FunctionTemplates.end()) return out;
		for (const Scope* s = call.scope; s && !member && !ns && out.Empty(); s = s->GetParent())
			for (const auto& def : it->second)
				if (!IsExtension(def.functionBody) && def.declScope == s) out.Push(&def);
		if (!out.Empty()) return out;
		for (const auto& def : it->second)
			if (IsExtension(def.functionBody) == member && (!ns || def.declScope == ns)) out.Push(&def);
		return out;
	}

	QueryResult<const FunctionRecord*> FunctionTemplates::Instantiate(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		const Core::String& name, const TemplateCall& call)
	{
		using Result = QueryResult<const FunctionRecord*>;
		auto deduced = Deduce(model, def, name, call);
		if (!deduced.Ok()) return Result::Fail(deduced.error, deduced.message);
		TemplateArgs args = deduced.value;
		auto made = Make(model, def, name, args, call);
		if (!made.Ok() || made.value->bodyNode) return made;
		return Result::Success(DefinitionOf(model, def, name, args, made.value, call));
	}

	QueryResult<TemplateArgs> FunctionTemplates::Deduce(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		const Core::String& name, const TemplateCall& call)
	{
		using Result = QueryResult<TemplateArgs>;
		const FunctionDefinitionNode& fn = *def.functionBody;
		auto written = TemplateArgumentResolver::Explicit(model, *def.params, call.written, call.unitId, nullptr, call.scope, name);
		if (!written.Ok()) return Result::Fail(written.error, written.message);

		TemplateDeduction deduction(model, *def.params, written.value);
		if (call.receiver && fn.IsExtensionMethod())
			if (auto r = deduction.FromReceiver(*fn.GetExtendsTypeName(), call.receiver); !r.Ok()) return Result::Fail(r.error, r.message);
		if (call.returnType && fn.GetReturnType())
			if (auto r = deduction.FromArgument(*fn.GetReturnType(), call.returnType); !r.Ok()) return Result::Fail(r.error, r.message);
		const auto& params = fn.GetParameters();
		const uint32 argCount = call.argTypes ? call.argTypes->Count() : 0;
		for (uint32 i = 0; i < params.Count() && i < argCount; ++i)
		{
			if (!params[i]->IsPack())
			{
				if (auto r = deduction.FromArgument(*params[i]->GetType(), (*call.argTypes)[i]); !r.Ok()) return Result::Fail(r.error, r.message);
				continue;
			}
			// `Ts... args` (D-19): every argument from here on is an element.
			if (i + 1 != params.Count())
				return Result::Fail(QueryError::NotAValue, "the parameter pack '" + params[i]->GetNameToken().GetText() + "' of '" + name +
					"' must be its last parameter");
			for (uint32 j = i; j < argCount; ++j)
				if (auto r = deduction.FromPackArgument(*params[i]->GetType(), (*call.argTypes)[j], j - i); !r.Ok()) return Result::Fail(r.error, r.message);
		}
		return Result::Success(deduction.Arguments());
	}

	QueryResult<const FunctionRecord*> FunctionTemplates::Make(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		const Core::String& name, TemplateArgs& args, const TemplateCall& call)
	{
		using Result = QueryResult<const FunctionRecord*>;
		Scope* parent = def.bindingParent ? def.bindingParent : const_cast<Scope*>(def.declScope ? def.declScope : model.m_Symbols->GetRoot());
		Scope* binding = model.m_Symbols->CreateScope(Scope::Kind::Block, "inst", parent);
		auto complete = TemplateArgumentResolver::CompleteAndBind(model, *def.params, args, call.unitId, binding, name);
		if (!complete.Ok()) return Result::Fail(complete.error, complete.message);
		args = complete.value;

		const Type* receiver = nullptr;
		if (def.functionBody->IsExtensionMethod())
		{
			auto extended = ExtendedType(model, def, binding, call.receiver, call.unitId);
			if (!extended.Ok()) return Result::Fail(extended.error, extended.message);
			receiver = extended.value;
		}
		return TemplateInstantiator().InstantiateFunction(model, def, name, args, binding, receiver, call.unitId);
	}

	bool FunctionTemplates::CouldDefine(const ProgramModel::TemplateDef& def, const ProgramModel::TemplateDef& decl)
	{
		// Same place, kind and arity: the signature for the same arguments decides.
		const FunctionDefinitionNode& d = *def.functionBody;
		const FunctionDefinitionNode& f = *decl.functionBody;
		return d.GetBody() && def.declScope == decl.declScope && d.IsExtensionMethod() == f.IsExtensionMethod() &&
			def.params->ParameterCount() == decl.params->ParameterCount() && d.GetParameters().Count() == f.GetParameters().Count();
	}

	const FunctionRecord* FunctionTemplates::DefinitionOf(const ProgramModel& model, const ProgramModel::TemplateDef& decl,
		const Core::String& name, const TemplateArgs& args, const FunctionRecord* declared, const TemplateCall& call)
	{
		// A template declared ahead of its definition: the definition made for the same arguments, when its
		// signature is the declaration's, is the one function both name.
		for (const ProgramModel::TemplateDef* def : Peers(model, decl, name))
		{
			if (!CouldDefine(*def, decl)) continue;
			TemplateArgs same = args;
			auto made = Make(model, *def, name, same, call);
			if (made.Ok() && made.value->containingType == declared->containingType && SameSignature(made.value->type, declared->type))
				return made.value;
		}
		return declared;
	}

	Core::Vector<const ProgramModel::TemplateDef*> FunctionTemplates::Peers(const ProgramModel& model, const ProgramModel::TemplateDef& decl,
		const Core::String& name)
	{
		if (decl.memberOf) return MemberTemplates::Named(model, decl.memberOf, name);
		Core::Vector<const ProgramModel::TemplateDef*> out;
		auto it = model.m_FunctionTemplates.find(name);
		if (it != model.m_FunctionTemplates.end())
			for (const auto& def : it->second) out.Push(&def);
		return out;
	}

	QueryResult<const Type*> FunctionTemplates::ExtendedType(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		Scope* binding, const Type* receiver, uint32 unitId)
	{
		// The type `extends` names for these arguments; the call's receiver must be it or derive from it.
		auto node = QualTypeNode::FromParts(def.functionBody->GetExtendsTypeName(), Ref<AstNode>(), {}, false);
		auto extended = model.ResolveTypeInternal(*node, unitId, binding, nullptr);
		if (!extended.Ok()) return extended;
		const Type* target = TypeHandle::UnwrapToValue(extended.value);
		const Type* actual = TypeHandle::UnwrapToValue(receiver);
		const ObjectType* targetObj = AsObject(target);
		const ObjectType* actualObj = AsObject(actual);
		const bool fits = target == actual || (targetObj && actualObj && ClassHierarchy::DerivesFrom(actualObj, targetObj));
		if (!fits) return QueryResult<const Type*>::Fail(QueryError::NotFound, "'" + def.functionBody->GetName()->ToString() +
			"' extends '" + target->GetName() + "', not '" + (actual ? actual->GetName() : Core::String("?")) + "'");
		return QueryResult<const Type*>::Success(target);
	}

	void FunctionTemplates::AttachAsMethod(const ProgramModel& model, const ObjectType* instance, const FunctionRecord* rec,
		const FunctionDefinitionNode& fn)
	{
		model.m_ExtensionMethods[instance].Push(rec);
		ObjectType::MethodInfo m;
		m.mName = rec->name;
		m.m_ReturnType = rec->type->GetReturnType();
		for (const auto& p : rec->type->GetParameters()) m.m_ParameterTypes.Push(p.m_Type);
		m.m_IsStatic = rec->isStatic;
		m.m_IsExtensionMethod = true;
		m.m_DeclaringNode = &fn;
		m.m_HasBody = rec->bodyNode != nullptr;
		m.m_Access = fn.GetAccessModifier();
		m.m_OperatorAnnotations = rec->operatorAnnotations;
		const_cast<ObjectType*>(instance)->AddMethod(m);
		model.m_MethodRecords[instance].Push(rec);
	}
} // namespace Scripting
