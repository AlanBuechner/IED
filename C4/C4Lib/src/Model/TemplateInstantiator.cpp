#include "Model/TemplateInstantiator.h"
#include "Model/Templates/ObjectTemplateBody.h"
#include "Model/Types/ValueContainment.h"
#include "Model/Templates/FunctionTemplates.h"
#include "Model/Templates/Specializations.h"
#include "Model/Templates/Packs.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/OperatorAnnotations.h"
#include "Model/TypeHandle.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"
#include "Parser/Nodes/Declarations/TemplateParameterNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <string>

namespace Scripting
{
	namespace
	{
		Core::String DefinitionKey(const Core::String& name, const void* def, const TemplateArgs& args)
		{
			return name + "@" + std::to_string(reinterpret_cast<uintptr_t>(def)) + ArgumentsKey(args);
		}

		// An instance's part of its members' symbols, from its arguments' names alone, so every program that makes
		// the instance names it alike (a script and its host, docs/runtime/04 section 4.1): `Box<Geo::Point>` is
		// `Box_TGeo_3a_3aPoint_E`; a member type of an instance names the instance it is in
		// (`Box<int32>::Node` is `Node_IBox_3cint32_3e_E`). The escaping never writes `_T` or `_E`, so two
		// instances never share one.
		Core::String InstanceToken(const ObjectType& instance, const Core::String& name)
		{
			if (!instance.GetTemplateArguments().Empty()) return name + "_T" + EscapedText(TemplateArgumentsText(instance)) + "_E";
			Core::String enclosing;
			for (const Core::String& part : instance.GetEnclosingNames()) enclosing += (enclosing.empty() ? "" : "::") + part;
			return name + "_I" + EscapedText(enclosing) + "_E";
		}

		Core::String ArgumentsText(const TemplateArgs& args)
		{
			Core::String text;
			for (const TemplateArg& a : args) text += (text.empty() ? "" : ", ") + (a.IsType() ? QualifiedName(a.type) : ConstantText(a.value));
			return text;
		}

		Core::Vector<ObjectType::TemplateArgument> ObjectArguments(const TemplateArgs& args)
		{
			Core::Vector<ObjectType::TemplateArgument> out;
			for (const TemplateArg& a : args)
			{
				ObjectType::TemplateArgument o;
				o.type = a.type;
				if (!a.type) o.value = a.value;
				out.Push(o);
			}
			return out;
		}

		bool SameSignature(const FunctionType* a, const FunctionType* b)
		{
			if (a->GetReturnType() != b->GetReturnType() || a->GetParameterCount() != b->GetParameterCount()) return false;
			for (uint32 i = 0; i < a->GetParameterCount(); ++i)
				if (a->GetParameters()[i].m_Type != b->GetParameters()[i].m_Type) return false;
			return true;
		}

		template <typename T>
		const T* Cached(const std::unordered_map<Core::String, const T*>& cache, const Core::String& key)
		{
			auto found = cache.find(key);
			return found != cache.end() ? found->second : nullptr;
		}
	}

	// --- type templates --------------------------------------------------------------------------------

	QueryResult<const Type*> TemplateInstantiator::InstantiateTypeTemplate(const ProgramModel& model, const Core::String& name, const Scope* declScope,
		const TemplateArgumentListNode& args, uint32 unitId, Scope* outerBinding, const Scope* argContext) const
	{
		const ProgramModel::TemplateDef* def = ChooseTypeDefinition(model, name, declScope);
		if (!def) return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a template: " + name);
		if (!def->params) return QueryResult<const Type*>::Fail(QueryError::NotFound, "template has no parameters: " + name);

		auto written = TemplateArgumentResolver::Explicit(model, *def->params, &args, unitId, outerBinding, argContext, name);
		if (!written.Ok()) return QueryResult<const Type*>::Fail(written.error, written.message);
		// Written in full, the arguments are the key as they are: an instance made before needs no binding scope.
		if (TemplateArgumentResolver::Complete(*def->params, written.value))
			if (const Type* made = Cached(model.m_TypeInstantiations, DefinitionKey(name, def, written.value))) return QueryResult<const Type*>::Success(made);
		Scope* binding = model.m_Symbols->CreateScope(Scope::Kind::Block, "inst", BodyParent(model, *def));
		auto complete = TemplateArgumentResolver::CompleteAndBind(model, *def->params, written.value, unitId, binding, name);
		if (!complete.Ok()) return QueryResult<const Type*>::Fail(complete.error, complete.message);

		const Core::String key = DefinitionKey(name, def, complete.value);
		if (const Type* cached = Cached(model.m_TypeInstantiations, key)) return QueryResult<const Type*>::Success(cached);
		model.Writes().Ask("a class template instance", [&model, name, declScope, &args, unitId, outerBinding, argContext] {
			(void)model.InstantiateTypeTemplate(name, declScope, args, unitId, outerBinding, argContext);
		});

		if (def->isAlias && def->aliasTarget)
		{
			// An alias template is transparent: the bound target IS the answer.
			auto target = model.ResolveTypeInternal(*def->aliasTarget, unitId, binding, nullptr);
			if (!target.Ok()) return target;
			model.m_InstantiationBindings.emplace(target.value, BindingsFor(model, *def->params, complete.value));
			model.m_TypeInstantiations.emplace(key, target.value);
			return target;
		}
		auto chosen = Specializations::ForType(model, *def, name, complete.value, unitId);
		if (!chosen.Ok()) return QueryResult<const Type*>::Fail(chosen.error, chosen.message);
		if (!chosen.value.def)
		{
			if (!def->objectBody || def->objectBody->IsDeclaration())
				return QueryResult<const Type*>::Fail(QueryError::NotFound, "'" + name + "' is declared but not defined, and no specialization of it "
					"is for these arguments");
			return InstantiateObject(model, *def, name, key, complete.value, complete.value, binding, unitId);
		}
		// A specialization's body binds its own parameters (`T` of `Box<T*>` for `Box<int32*>`).
		Scope* own = model.m_Symbols->CreateScope(Scope::Kind::Block, "inst", BodyParent(model, *chosen.value.def));
		TemplateArgumentResolver::BindAll(model, own, *chosen.value.def->params, chosen.value.args);
		return InstantiateObject(model, *chosen.value.def, name, key, complete.value, chosen.value.args, own, unitId);
	}

	// Where an instance's names resolve past its parameters: the template's own declaring scope (or the class
	// instance a member template belongs to), never the scope of whichever use made the instance.
	Scope* TemplateInstantiator::BodyParent(const ProgramModel& model, const ProgramModel::TemplateDef& def)
	{
		if (def.bindingParent) return def.bindingParent;
		return const_cast<Scope*>(def.declScope ? def.declScope : model.m_Symbols->GetRoot());
	}

	const ProgramModel::TemplateDef* TemplateInstantiator::ChooseTypeDefinition(const ProgramModel& model, const Core::String& name, const Scope* declScope) const
	{
		// Same-named templates in different scopes coexist (a global `Wedge` and `Weld::Wedge`): the one declared
		// innermost on the lookup chain from `declScope` wins, like ordinary name lookup; else the first. In one
		// scope a definition wins over a declaration of the same template.
		auto it = model.m_TypeTemplates.find(name);
		if (it == model.m_TypeTemplates.end() || it->second.empty()) return nullptr;
		auto defined = [](const ProgramModel::TemplateDef& d) { return d.isAlias || (d.objectBody && !d.objectBody->IsDeclaration()); };
		for (const Scope* s = declScope; s; s = s->GetParent())
		{
			const ProgramModel::TemplateDef* found = nullptr;
			for (const auto& candidate : it->second)
				if (DeclaredIn(candidate, s) && (!found || (!defined(*found) && defined(candidate)))) found = &candidate;
			if (found) return found;
		}
		return &it->second[0];
	}

	QueryResult<const Type*> TemplateInstantiator::InstantiateObject(const ProgramModel& model, const ProgramModel::TemplateDef& def, const Core::String& name,
		const Core::String& key, const TemplateArgs& instanceArgs, const TemplateArgs& args, Scope* binding, uint32 unitId) const
	{
		model.Writes().Ask("a class template instance");
		ObjectType* obj = new ObjectType(TypeKind::Object, name);
		obj->SetAbstract(def.objectBody->IsAbstract());
		obj->SetFinal(def.objectBody->IsFinal());
		ApplyDeclaredIdentity(*obj, *def.objectBody, def.declScope);
		obj->SetTemplateArguments(ObjectArguments(instanceArgs));
		obj->SetDefined(true);

		// The instance is cached before its body is walked: a template may name its own instance inside its
		// body (`T Combine(const Shape<T>& o)`), which must find this type instead of recursing.
		const Type* self = model.m_Types->Own(obj);
		model.m_ObjectTypes.Push(static_cast<const ObjectType*>(obj));
		model.m_TypeInstantiations.emplace(key, self);
		// Inside its body the template's bare name is this instance (C++'s injected class name).
		binding->Register(name, SymbolInfo{ SymbolKind::Type, self });

		ObjectTemplateBody body(model, *obj, def, name, args, { binding, InstanceScope(model, *obj, def, name), unitId });
		if (auto built = body.Build(); !built.Ok())
		{
			model.m_TypeInstantiations.erase(key); // a later use may retry
			return QueryResult<const Type*>::Fail(built.error, "cannot instantiate " + name + ": " + built.message);
		}
		if (auto cycle = ValueContainment::CheckNotSelfContaining(obj); !cycle.Ok())
			return QueryResult<const Type*>::Fail(cycle.error, "cannot instantiate " + name + ": " + cycle.message);
		// Query-time instantiation runs after the program's layouts were finalized: lay this one out now.
		obj->CalculateLayout();
		InstantiationBindings bindings = BindingsFor(model, *def.params, args);
		bindings.typeParams.Push({ name, self });
		model.m_InstantiationBindings.emplace(self, bindings);
		FunctionTemplates::AttachExtensions(model, obj, unitId);
		return QueryResult<const Type*>::Success(self);
	}

	QueryResult<const Type*> TemplateInstantiator::InstantiateMemberType(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		const Core::String& name, const MemberTypeSite& site) const
	{
		model.Writes().Ask("a member type");
		ObjectType* obj = new ObjectType(TypeKind::Object, name);
		obj->SetAbstract(def.objectBody->IsAbstract());
		obj->SetFinal(def.objectBody->IsFinal());
		ApplyDeclaredIdentity(*obj, *def.objectBody, def.declScope);
		obj->SetEnclosingNames(site.enclosing);
		obj->SetDefined(true);
		const Type* self = model.m_Types->Own(obj);
		model.m_ObjectTypes.Push(static_cast<const ObjectType*>(obj));
		Scope* binding = model.m_Symbols->CreateScope(Scope::Kind::Block, "member", BodyParent(model, def));
		binding->Register(name, SymbolInfo{ SymbolKind::Type, self });

		const TemplateArgs none;
		ObjectTemplateBody body(model, *obj, def, name, none, { binding, InstanceScope(model, *obj, def, name), site.unitId, &site.ownerBindings });
		if (auto built = body.Build(); !built.Ok())
			return QueryResult<const Type*>::Fail(built.error, "cannot build " + name + ": " + built.message);
		obj->CalculateLayout();
		InstantiationBindings bindings = site.ownerBindings;
		bindings.typeParams.Push({ name, self });
		model.m_InstantiationBindings.emplace(self, bindings);
		FunctionTemplates::AttachExtensions(model, obj, site.unitId);
		return QueryResult<const Type*>::Success(self);
	}

	// A definition belongs to scope `s` when it is declared there, or - a template nested in a class template -
	// when it sits on the instance whose binding layer `s` is.
	bool TemplateInstantiator::DeclaredIn(const ProgramModel::TemplateDef& def, const Scope* s)
	{
		return def.declScope == s || (def.bindingParent && def.bindingParent == s);
	}

	Scope* TemplateInstantiator::InstanceScope(const ProgramModel& model, const ObjectType& instance, const ProgramModel::TemplateDef& def,
		const Core::String& name)
	{
		Scope* parent = const_cast<Scope*>(def.declScope ? def.declScope : model.m_Symbols->GetRoot());
		Scope* scope = model.m_Symbols->CreateScope(Scope::Kind::Class, InstanceToken(instance, name), parent);
		scope->objectType = &instance;
		model.m_TypeScopes[&instance] = scope;
		return scope;
	}

	// --- function templates ----------------------------------------------------------------------------

	Core::String TemplateInstantiator::FunctionKey(const Core::String& name, const ProgramModel::TemplateDef& def, const TemplateArgs& args,
		const Type* receiver)
	{
		Core::String key = DefinitionKey(name, &def, args);
		if (receiver) key += "@" + std::to_string(reinterpret_cast<uintptr_t>(receiver));
		return key;
	}

	const FunctionRecord* TemplateInstantiator::MadeFunction(const ProgramModel& model, const ProgramModel::TemplateDef& def, const Core::String& name,
		const TemplateArgs& args) const
	{
		return Cached(model.m_FunctionInstantiations, FunctionKey(name, def, args, nullptr));
	}

	QueryResult<const FunctionRecord*> TemplateInstantiator::InstantiateFunction(const ProgramModel& model, const ProgramModel::TemplateDef& def,
		const Core::String& name, const TemplateArgs& args, Scope* binding, const Type* receiver, uint32 unitId) const
	{
		const Core::String key = FunctionKey(name, def, args, receiver);
		if (const FunctionRecord* cached = Cached(model.m_FunctionInstantiations, key)) return QueryResult<const FunctionRecord*>::Success(cached);
		model.Writes().Ask("a function template instance", [&model, &def, name, args, binding, receiver, unitId] {
			(void)TemplateInstantiator().InstantiateFunction(model, def, name, args, binding, receiver, unitId);
		});

		auto expanded = FunctionSignature(model, *def.functionBody, binding, unitId);
		if (!expanded.Ok()) return QueryResult<const FunctionRecord*>::Fail(expanded.error, "cannot instantiate " + name + ": " + expanded.message);
		QueryResult<const FunctionType*> sig(expanded.value.type);
		// An explicit specialization (`template<> int32 F<int32>(int32 v)`) is the body for its arguments (D-20).
		auto special = Specializations::ForFunction(model, def, name, args, unitId);
		if (!special.Ok()) return QueryResult<const FunctionRecord*>::Fail(special.error, special.message);
		if (special.value)
		{
			auto own = FunctionSignature(model, *special.value->functionBody, binding, unitId);
			if (!own.Ok()) return QueryResult<const FunctionRecord*>::Fail(own.error, own.message);
			if (!SameSignature(own.value.type, sig.value))
				return QueryResult<const FunctionRecord*>::Fail(QueryError::NotAValue, "the specialization '" + name +
					special.value->pattern->ToString() + "' is '" + own.value.type->ToString() + "', but '" + name + "' for those arguments is '" +
					sig.value->ToString() + "'");
			expanded = own;
		}
		const FunctionDefinitionNode& fn = special.value ? *special.value->functionBody : *def.functionBody;

		FunctionRecord rec;
		rec.type = sig.value;
		rec.scope = def.declScope;
		rec.bodyNode = fn.GetBody().Get();
		rec.declarations.Push(&fn);
		rec.isNoexcept = fn.IsNoexcept();
		rec.isStatic = fn.IsStatic();
		rec.templateOrigin = TemplateOrigin::FunctionTemplate;
		rec.definingUnit = 0;
		rec.name = name;
		rec.mangledName = CanonicalSymbolName(def.declScope, name) + "_T" + EscapedText(ArgumentsText(args)) + "_E" +
			(receiver ? "_on_" + EscapedText(QualifiedName(receiver)) : Core::String()) + "_" + sig.value->OverloadSuffix();
		rec.minArity = sig.value->GetParameterCount() - fn.TrailingDefaultCount();
		const std::vector<bool> written = fn.ParameterConstness();
		for (uint32 i = 0; i < sig.value->GetParameterCount(); ++i)
		{
			const auto& p = fn.GetParameters()[expanded.value.source[i]];
			const Core::String& expandedName = sig.value->GetParameters()[i].mName;
			rec.paramNames.Push(!expandedName.empty() ? expandedName : (p->HasName() ? p->GetNameToken().GetText() : Core::String("arg")));
			rec.paramIsConst.push_back(written[expanded.value.source[i]]);
			rec.paramDefaults.Push(p->GetDefaultValue().Get());
		}
		rec.isConst = (receiver || def.memberOf) && fn.IsConstMethod();
		RecordParameterPacks(rec, fn, expanded.value);
		for (const auto& ann : fn.GetAnnotations()) CollectOperatorAnnotations(ann.Get(), fn.GetParameters().Count(), rec.operatorAnnotations);
		if (def.memberOf)
		{
			rec.containingType = def.memberOf; // a member template's instance is a method of its owner
			rec.access = fn.GetAccessModifier();
		}
		else if (receiver)
		{
			const Type* r = TypeHandle::UnwrapToValue(receiver);
			rec.isExtensionMethod = true;
			if (r->GetKind() == TypeKind::Object) rec.containingType = static_cast<const ObjectType*>(r);
			else rec.primitiveReceiverType = r;
		}

		const FunctionRecord* stored = model.m_Symbols->AddFunctionRecord(rec);
		model.m_RecordBySignature.emplace(rec.type, stored);
		if (rec.primitiveReceiverType) model.m_PrimitiveExtensions[rec.primitiveReceiverType].Push(stored);
		model.m_FunctionInstantiations.emplace(key, stored);
		model.m_FuncInstantiationBindings.emplace(stored, WithOwnerNames(BindingsFor(model, *def.params, args), def.ownerNames)); // the body binds T := int32 too
		model.m_TemplateOfInstance[stored] = &def;
		return QueryResult<const FunctionRecord*>::Success(stored);
	}

	InstantiationBindings TemplateInstantiator::WithOwnerNames(InstantiationBindings own, const InstantiationBindings& ownerNames)
	{
		for (const auto& p : ownerNames.typeParams) own.typeParams.Push(p);
		for (const auto& p : ownerNames.valueParams) own.valueParams.Push(p);
		for (const auto& p : ownerNames.packParams) own.packParams.Push(p);
		return own;
	}

	void TemplateInstantiator::RecordParameterPacks(FunctionRecord& rec, const FunctionDefinitionNode& fn, const Signature& signature)
	{
		for (uint32 index = 0; index < fn.GetParameters().Count(); ++index)
		{
			if (!fn.GetParameters()[index]->IsPack()) continue;
			FunctionRecord::ParameterPack pack;
			pack.name = fn.GetParameters()[index]->GetNameToken().GetText();
			pack.first = static_cast<uint32>(signature.source.size());
			for (uint32 i = 0; i < signature.source.size(); ++i)
				if (signature.source[i] == index) { pack.first = std::min(pack.first, i); ++pack.count; }
			rec.parameterPacks.Push(pack);
		}
	}

	QueryResult<TemplateInstantiator::Signature> TemplateInstantiator::FunctionSignature(const ProgramModel& model, const FunctionDefinitionNode& fn,
		Scope* binding, uint32 unitId) const
	{
		// A parameter pack (`Ts... args`) is one parameter per element, named `args#0`, `args#1`, ... (D-19).
		using Result = QueryResult<Signature>;
		auto rt = model.ResolveTypeInternal(*fn.GetReturnType(), unitId, binding, nullptr);
		if (!rt.Ok()) return Result::Fail(rt.error, rt.message);
		Signature out;
		Core::Vector<FunctionType::ParameterInfo> params;
		for (uint32 index = 0; index < fn.GetParameters().Count(); ++index)
		{
			const auto& p = fn.GetParameters()[index];
			if (!p->IsPack())
			{
				auto pt = model.ResolveTypeInternal(*p->GetType(), unitId, binding, nullptr);
				if (!pt.Ok()) return Result::Fail(pt.error, pt.message);
				FunctionType::ParameterInfo pi; pi.mName = p->GetNameToken().GetText(); pi.m_Type = pt.value;
				params.Push(pi);
				out.source.push_back(index);
				continue;
			}
			auto packs = Packs::In(model, p->GetType().Get(), binding);
			if (!packs.Ok()) return Result::Fail(packs.error, packs.message);
			for (uint32 e = 0; e < Packs::Length(packs.value); ++e)
			{
				auto pt = model.ResolveTypeInternal(*p->GetType(), unitId, Packs::ElementScope(model, packs.value, e, binding), nullptr);
				if (!pt.Ok()) return Result::Fail(pt.error, pt.message);
				FunctionType::ParameterInfo pi; pi.mName = p->GetNameToken().GetText() + "#" + std::to_string(e); pi.m_Type = pt.value;
				params.Push(pi);
				out.source.push_back(index);
			}
		}
		model.Writes().Ask("a function template instance's signature");
		FunctionType* sig = new FunctionType(rt.value, params, fn.IsVariadic());
		out.type = model.m_Types->Own(sig);
		return Result::Success(out);
	}

} // namespace Scripting
