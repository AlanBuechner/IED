#include "Model/Builder/FreeFunctionResolver.h"
#include "Model/Builder/LinkSymbol.h"
#include "Model/Types/QualifiedName.h"
#include "Model/TypeHandle.h"
#include "Model/OperatorAnnotations.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Scripting
{
	namespace
	{
		// `int32 Twice(int x)` next to `int32 Twice(int32 x)`: the signature shows why the two collide.
		Core::String DuplicateDefinitionMessage(const Core::String& name, const FunctionType& signature,
			const AstNode& first, const AstNode& second)
		{
			Core::String message = "Duplicate definition of '" + name + "' with signature " + signature.ToString();
			if (first.startLocation.sourceId == second.startLocation.sourceId)
				message += " (the first body starts on line " + std::to_string(first.startLocation.line) + ")";
			return message;
		}

		// A modifier that only means something on a method of a type (D-29), or null.
		const char* MethodOnlyModifier(const FunctionDefinitionNode& fn)
		{
			if (fn.IsAbstract()) return "abstract";
			if (fn.IsVirtual()) return "virtual";
			if (fn.IsOverride()) return "override";
			if (fn.IsFinal()) return "final";
			return nullptr;
		}

		// `Math::Count` for a function registered in a namespace; a file-scope function keeps its bare name.
		Core::String QualifiedName(const Scope* scope, const Core::String& simpleName)
		{
			Core::String name = simpleName;
			for (const Scope* s = scope; s && s->GetKind() != Scope::Kind::File; s = s->GetParent())
				if (!s->GetName().empty()) name = s->GetName() + "::" + name;
			return name;
		}

		// A file-scope `static` free function has internal linkage (D4): same-named statics in different files are
		// distinct symbols, so only a declaration and definition in one file merge.
		bool IsFileStatic(const FunctionDefinitionNode& fn, const Scope* scope)
		{
			return fn.IsStatic() && !fn.IsExtensionMethod() && scope && scope->GetKind() == Scope::Kind::File;
		}

		template<typename Pending, typename Settle>
		bool SettleEach(Core::Vector<Pending>& pending, Settle settle)
		{
			bool changed = false;
			for (uint32 i = 0; i < pending.Count(); )
			{
				if (!settle(pending[i])) { ++i; continue; }
				pending.Remove(i);
				changed = true;
			}
			return changed;
		}
	}

	bool FreeFunctionResolver::Resolve()
	{
		bool changed = SettleEach(m_Builder.m_PendingFuncs, [this](const ModelBuilder::PendingFunc& f) { return ResolveFree(f); });
		changed |= SettleEach(m_Builder.m_PendingQualifiedFuncs, [this](const ModelBuilder::PendingQualifiedFunc& f) { return ResolveQualified(f); });
		return changed;
	}

	// In the defining scope, so a parameter type binds to that scope's own declarations (`Math::AddX(const Vec&)`
	// finds Math's Vec, not a file-scope one).
	bool FreeFunctionResolver::ResolveSignature(const FunctionDefinitionNode& fn, uint32 unitId, const Scope* scope, Signature& out) const
	{
		auto returned = m_Ctx.model->ResolveTypeInContext(*fn.GetReturnType(), unitId, scope);
		if (!returned.Ok()) return false;
		Core::Vector<const Type*> types;
		uint32 defaults = 0;
		for (const auto& p : fn.GetParameters())
		{
			auto type = m_Ctx.model->ResolveTypeInContext(*p->GetType(), unitId, scope);
			if (!type.Ok()) return false;
			FunctionType::ParameterInfo info;
			info.mName = p->HasName() ? p->GetNameToken().GetText() : ("arg" + std::to_string(out.params.Count()));
			info.m_Type = type.value;
			out.params.Push(info);
			types.Push(type.value);
			if (p->HasDefaultValue()) ++defaults;
		}
		out.returnType = returned.value;
		out.type = m_Ctx.model->m_Types->Function(returned.value, types);
		out.minArity = out.params.Count() > defaults ? out.params.Count() - defaults : 0;
		return true;
	}

	// Resolved in the defining scope: an extension method inside a namespace names its target by bare name.
	FreeFunctionResolver::Receiver FreeFunctionResolver::ReceiverOf(const ModelBuilder::PendingFunc& f) const
	{
		if (!f.fn->IsExtensionMethod() || !f.fn->GetExtendsTypeName()) return {};
		auto target = QualTypeNode::FromParts(f.fn->GetExtendsTypeName(), nullptr, {}, false);
		auto resolved = m_Ctx.model->ResolveTypeInContext(*target, f.unitId, f.scope);
		if (!resolved.Ok() || !resolved.value) return {};
		const Type* value = TypeHandle::UnwrapToValue(resolved.value);
		if (value->GetKind() == TypeKind::Object) return { static_cast<const ObjectType*>(value), nullptr };
		if (value->IsNumeric()) return { nullptr, value };
		return {};
	}

	const FunctionRecord* FreeFunctionResolver::DeclaredMethod(const ObjectType& owner, const Core::String& name, const FunctionType* signature, bool isConst) const
	{
		auto records = m_Ctx.model->m_MethodRecords.find(&owner);
		if (records == m_Ctx.model->m_MethodRecords.end()) return nullptr;
		const auto& methods = owner.GetMethods();
		for (uint32 i = 0; i < owner.GetMethodCount() && i < records->second.Count(); ++i)
		{
			const FunctionRecord* rec = records->second[i];
			if (rec && methods[i].mName == name && rec->type == signature && rec->isConst == isConst) return rec;
		}
		return nullptr;
	}

	// The definition's body joins its declaration's record; the body's parameter names are the definition's, which C++
	// lets differ from the declaration's.
	void FreeFunctionResolver::AttachDefinition(const FunctionRecord& rec, const FunctionDefinitionNode& fn, uint32 unitId, const Core::String& shownName) const
	{
		const AstNode* body = fn.GetBody().Get();
		if (body && rec.bodyNode && rec.bodyNode != body)
			m_Builder.ErrorAt(m_Ctx, &fn, DuplicateDefinitionMessage(shownName, *rec.type, *rec.bodyNode, fn));
		rec.declarations.Push(&fn);
		if (!body) return;
		rec.bodyNode = body;
		rec.definingUnit = unitId; // lowered with the definition file's include closure
		if (fn.GetParameters().Count() != rec.paramNames.Count()) return;
		for (uint32 i = 0; i < fn.GetParameters().Count(); ++i)
			if (fn.GetParameters()[i]->HasName()) rec.paramNames[i] = fn.GetParameters()[i]->GetNameToken().GetText();
		rec.paramIsConst = fn.ParameterConstness();
	}

	bool FreeFunctionResolver::ResolveFree(const ModelBuilder::PendingFunc& f)
	{
		Signature sig;
		if (!ResolveSignature(*f.fn, f.unitId, f.scope, sig)) return false;
		const Receiver receiver = ReceiverOf(f);
		if (MergeWithDeclaration(f, sig, receiver)) return true;
		// A type's own method declared in its body (`[operator("create")] void Construct();`) defined by an `extends`.
		if (const FunctionRecord* declared = receiver.object ? DeclaredMethod(*receiver.object, f.name, sig.type, f.fn->IsConstMethod()) : nullptr)
		{
			AttachDefinition(*declared, *f.fn, f.unitId, f.name);
			return true;
		}
		const FunctionRecord* stored = AddFreeRecord(f, sig, receiver);
		if (auto test = m_Ctx.model->MutableTests().Collect(*f.fn, *stored, f.unitId); !test.Ok())
			m_Builder.ErrorAt(m_Ctx, f.fn, test.message);
		return true;
	}

	// A declaration and its definition are one record: the same scope, interned signature and receiver.
	const FunctionRecord* FreeFunctionResolver::MergeWithDeclaration(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const
	{
		const SymbolInfo* existing = f.scope->Find(f.name);
		if (!existing || existing->kind != SymbolKind::Function) return nullptr;
		const bool fileStatic = IsFileStatic(*f.fn, f.scope);
		for (const FunctionRecord* rec : existing->functions)
		{
			if (rec->type != sig.type || (fileStatic && rec->declUnit != f.unitId)) continue;
			const bool sameReceiver = rec->isExtensionMethod
				? rec->containingType == receiver.object && rec->primitiveReceiverType == receiver.primitive
				: !f.fn->IsExtensionMethod();
			if (!sameReceiver) continue;
			AttachDefinition(*rec, *f.fn, f.unitId, QualifiedName(f.scope, f.name));
			if (!f.fn->GetBody()) rec->declUnit = f.unitId; // a declaration makes the name visible to its includers
			return rec;
		}
		return nullptr;
	}

	const FunctionRecord* FreeFunctionResolver::AddFreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver)
	{
		const FunctionRecord rec = FreeRecord(f, sig, receiver);
		CheckFreeModifiers(f, rec);
		if (f.fn->IsVariadic()) sig.type->SetVariadic(true); // overload resolution accepts the extra arguments
		const FunctionRecord* stored = m_Ctx.model->Symbols()->AddFunctionRecord(rec);
		m_Ctx.model->m_RecordBySignature.emplace(sig.type, stored);
		if (receiver.primitive) m_Ctx.model->m_PrimitiveExtensions[receiver.primitive].Push(stored);
		if (receiver.object) AddExtensionMethod(f, sig, stored, *receiver.object);
		f.scope->Register(f.name, SymbolInfo{ SymbolKind::Function }, stored); // grows the overload set (doc 08)
		return stored;
	}

	FunctionRecord FreeFunctionResolver::FreeRecord(const ModelBuilder::PendingFunc& f, const Signature& sig, const Receiver& receiver) const
	{
		const FunctionDefinitionNode& fn = *f.fn;
		FunctionRecord rec;
		rec.type = sig.type;
		rec.scope = f.scope;
		rec.bodyNode = fn.GetBody().Get();
		rec.declarations.Push(&fn);
		rec.definingUnit = f.unitId;
		rec.declUnit = f.unitId;
		rec.isBuiltin = f.unitId == m_Ctx.model->PreludeUnit();
		rec.mangledName = FunctionSymbol(f.scope, f.name, *sig.type);
		// Same-named file-scope statics in different files must not share one symbol.
		if (fn.IsStatic() && f.scope->GetKind() == Scope::Kind::File) rec.mangledName += "_u" + std::to_string(f.unitId);
		if (Core::String link = LinkSymbolOf(fn.GetAnnotations()); !link.empty())
		{
			rec.mangledName = link;
			rec.isLinked = true;
		}
		rec.name = f.name;
		for (const auto& annotation : fn.GetAnnotations()) CollectGroupAnnotations(annotation.Get(), rec.groups);
		rec.minArity = sig.minArity;
		for (const auto& p : sig.params) rec.paramNames.Push(p.mName);
		for (const auto& p : fn.GetParameters()) rec.paramDefaults.Push(p->GetDefaultValue().Get());
		rec.paramIsConst = fn.ParameterConstness();
		rec.isStatic = fn.IsStatic();
		rec.isVirtual = fn.IsVirtual();
		rec.isNoexcept = fn.IsNoexcept();
		for (const auto& annotation : fn.GetAnnotations())
			CollectOperatorAnnotations(annotation.Get(), fn.GetParameters().Count(), rec.operatorAnnotations);
		if (!receiver.object && !receiver.primitive) return rec;
		rec.isExtensionMethod = true;
		rec.containingType = receiver.object;
		rec.primitiveReceiverType = receiver.primitive;
		rec.isConst = fn.IsConstMethod();
		if (rec.isLinked) return rec;
		rec.mangledName += "_on_" + SymbolToken(receiver.object ? static_cast<const Type*>(receiver.object) : receiver.primitive);
		if (rec.isConst) rec.mangledName += "_const";
		return rec;
	}

	void FreeFunctionResolver::CheckFreeModifiers(const ModelBuilder::PendingFunc& f, const FunctionRecord& rec) const
	{
		if (const char* modifier = MethodOnlyModifier(*f.fn))
			m_Builder.ErrorAt(m_Ctx, f.fn, "'" + f.name + "' is not a method of a type: '" + modifier + "' applies only to methods");
		if (rec.isExtensionMethod && rec.isStatic)
			m_Builder.ErrorAt(m_Ctx, f.fn, "'" + f.name + "' extends a type and is static: an extension method is called on an object");
		if (rec.isExtensionMethod) return;
		if (f.fn->IsConstMethod()) m_Builder.ErrorAt(m_Ctx, f.fn, "'" + f.name + "' is not a method: only a method can be const");
		if (!rec.operatorAnnotations.Empty())
			m_Builder.ErrorAt(m_Ctx, f.fn, "'" + f.name + "' is a free function: operators must be methods or extension methods (write 'extends T')");
	}

	// In the target's method list (so member and operator lookup walk it like any member) and its extension table.
	void FreeFunctionResolver::AddExtensionMethod(const ModelBuilder::PendingFunc& f, const Signature& sig, const FunctionRecord* stored, const ObjectType& target)
	{
		m_Ctx.model->m_ExtensionMethods[&target].Push(stored);
		ObjectType::MethodInfo m;
		m.mName = f.name;
		m.m_ReturnType = sig.returnType;
		for (const auto& p : sig.params) m.m_ParameterTypes.Push(p.m_Type);
		m.m_IsStatic = f.fn->IsStatic();
		m.m_IsConst = f.fn->IsConstMethod();
		m.m_IsExtensionMethod = true;
		m.m_Access = f.fn->GetAccessModifier();
		m.m_DeclaringNode = f.fn;
		m.m_HasBody = f.fn->GetBody() != nullptr;
		m.m_OperatorAnnotations = stored->operatorAnnotations;
		const_cast<ObjectType&>(target).AddMethod(m);
		m_Ctx.model->m_MethodRecords[&target].Push(stored);
	}
} // namespace Scripting
