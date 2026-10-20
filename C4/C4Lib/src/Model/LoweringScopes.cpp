#include "Model/LoweringScopes.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/OverloadResolution.h"
#include "Model/ConstEvaluator.h"
#include "Model/TypeResolver.h" // type positions in local decls resolve through the resolver
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h" // complete type: bodies are stored as pointers

namespace Scripting
{
	// ---------------------------------------------------------------------------
	// LOWERING-TIME SCOPES (doc 06)
	// ---------------------------------------------------------------------------
	ScopeGuard LoweringScopes::PushScope(ProgramModel& model, Scope::Kind kind, const Core::String& name)
	{
		return model.m_Symbols->PushScope(kind, name);
	}

	bool LoweringScopes::RegisterLocal(ProgramModel& model, const Core::String& name, SymbolInfo info)
	{
		Scope* cur = model.m_Symbols->Current();
		if (!cur) return false;
		info.definingUnit = 0; // locals are visible to the lowering of this function only
		return cur->Register(name, info);
	}

	const SymbolInfo* LoweringScopes::FindLocal(const ProgramModel& model, const Core::String& name) const
	{
		// Walk ONLY the lowering chain (Function/Block layers pushed during a body's lowering),
		// innermost first. Stop at the first permanent (File/Namespace/Class) layer: globals are
		// found by the visibility-aware path, not here - so a local shadows a global but never is
		// confused with one. A body lowered inside another function's lowering (a lambda, a local
		// function) is its own frame: the enclosing function's types, aliases and template bindings stay
		// visible, its variables do not (a lambda reaches them only through its captures).
		bool enclosingFrame = false;
		for (const Scope* s = model.m_Symbols->Current(); s; s = s->GetParent())
		{
			if (s->GetKind() != Scope::Kind::Function && s->GetKind() != Scope::Kind::Block) break;
			const auto& entries = s->Entries(name);
			if (!entries.Empty())
			{
				const SymbolInfo* found = entries[0].get();
				return enclosingFrame && found->IsFrameVariable() ? nullptr : found;
			}
			if (s->GetKind() == Scope::Kind::Function) enclosingFrame = true;
		}
		return nullptr;
	}

	// ---------------------------------------------------------------------------
	// FUNCTION-LOCAL DECLARATIONS (doc 06): a struct/enum/function written inside a body becomes a
	// concrete type/signature in the store, with its NAME registered in the CURRENT scope layer so
	// it is visible exactly where declared and vanishes when the layer pops. Types resolve against
	// the current scope, so a local may name an earlier local or any global. Locals are unit-
	// agnostic (definingUnit 0): they belong to the lowering of one function, not a file.
	// ---------------------------------------------------------------------------
	QueryResult<const Type*> LoweringScopes::RegisterLocalType(ProgramModel& model, const AstNode& decl)
	{
		Scope* layer = model.m_Symbols->Current();
		if (!layer) return QueryResult<const Type*>::Fail(QueryError::NotFound, "no scope to register a local type in");

		if (const auto* obj = dynamic_cast<const ObjectDefinitionNode*>(&decl))
		{
			const Core::String name = obj->GetNameToken().GetText();
			ObjectType* t = new ObjectType(TypeKind::Object, name);
			t->SetAbstract(obj->IsAbstract());
			t->SetFinal(obj->IsFinal());
			ApplyLocalIdentity(*t, *obj, layer);
			for (const auto& b : obj->GetBaseClasses())
			{
				auto bt = TypeResolver().ResolveTypeInternal(model, *b.m_Type, 0, layer, nullptr);
				if (!bt.Ok()) { delete t; return QueryResult<const Type*>::Fail(bt.error, bt.message); }
				if (const auto* bo = dynamic_cast<const ObjectType*>(bt.value)) t->AddBaseType(bo);
			}
			if (auto rule = ClassHierarchy::CheckBaseList(t); !rule.Ok()) { delete t; return QueryResult<const Type*>::Fail(rule.error, rule.message); }
			for (const auto& member : obj->GetBodyMembers())
			{
				const auto* child = member.Get();
				if (const auto* al = dynamic_cast<const AliasDefinitionNode*>(child))
				{
					auto at = TypeResolver().ResolveTypeInternal(model, *al->GetAliasedType(), 0, layer, nullptr);
					if (!at.Ok()) { delete t; return QueryResult<const Type*>::Fail(at.error, at.message); }
					SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = at.value; sym.definingUnit = 0;
					layer->Register(al->GetNameToken().GetText(), sym); // local alias usable by later members
				}
				else if (const auto* var = dynamic_cast<const VariableDefinitionNode*>(child))
				{
					auto vt = TypeResolver().ResolveTypeInternal(model, *var->GetType(), 0, layer, nullptr);
					if (!vt.Ok()) { delete t; return QueryResult<const Type*>::Fail(vt.error, vt.message); }
					// A comma list (`int32 x, y;`) declares MULTIPLE fields - register every declarator.
					// (The global-struct path does the same; using only GetNameToken here dropped all but
					// the first field of a local type.)
					for (const auto& d : var->GetDeclarators())
					{
						ObjectType::FieldInfo f;
						f.mName = d.m_NameToken.GetText();
						f.m_Type = vt.value;
						f.m_IsStatic = var->IsStatic();
						f.m_IsConst = var->IsConst();
						f.m_DeclaringNode = var;
						if (!f.m_IsStatic) f.m_Initializer = d.m_Initializer.Get();
						t->AddField(f);
					}
				}
				else if (const auto* fn = dynamic_cast<const FunctionDefinitionNode*>(child))
				{
					auto rt = TypeResolver().ResolveTypeInternal(model, *fn->GetReturnType(), 0, layer, nullptr);
					if (!rt.Ok()) { delete t; return QueryResult<const Type*>::Fail(rt.error, rt.message); }
					ObjectType::MethodInfo m;
					m.mName = fn->GetName()->GetFirstIdentifierToken().GetText();
					m.m_ReturnType = rt.value;
					Core::Vector<FunctionType::ParameterInfo> paramInfos;
					for (const auto& p : fn->GetParameters())
					{
						auto pt = TypeResolver().ResolveTypeInternal(model, *p->GetType(), 0, layer, nullptr);
						if (!pt.Ok()) { delete t; return QueryResult<const Type*>::Fail(pt.error, pt.message); }
						m.m_ParameterTypes.Push(pt.value);
						FunctionType::ParameterInfo pi; pi.mName = p->GetNameToken().GetText(); pi.m_Type = pt.value;
						paramInfos.Push(pi);
					}
					m.m_IsStatic = fn->IsStatic();
					m.m_IsVirtual = fn->IsVirtual();
					m.m_IsConst = fn->IsConstMethod();
					t->AddMethod(m);

					FunctionType* sig = new FunctionType(rt.value, paramInfos);
					FunctionRecord rec;
					rec.type = model.m_Types->Own(sig);
					rec.scope = layer;
					rec.containingType = t;
					rec.isStatic = fn->IsStatic();
					rec.isVirtual = fn->IsVirtual();
					rec.isNoexcept = fn->IsNoexcept();
					rec.isConst = fn->IsConstMethod();
					rec.bodyNode = fn->GetBody().Get();
					rec.declarations.Push(fn);
					rec.definingUnit = 0;
					rec.mangledName = MethodSymbol(nullptr, *t, m.mName, *sig, rec.isConst);
					rec.mangledName = CanonicalSymbolName(layer, rec.mangledName); // the enclosing function's path keeps local types apart
					const FunctionRecord* stored = model.m_Symbols->AddFunctionRecord(rec);
					model.m_MethodRecords[t].Push(stored);
					model.m_RecordBySignature.emplace(rec.type, stored);
				}
			}
			const Type* owned = model.m_Types->Own(t);
			t->CalculateLayout(); // doc 06: a local type is queryable with layout computed on demand
			model.m_ObjectTypes.Push(static_cast<const ObjectType*>(t));
			SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = owned; sym.definingUnit = 0; sym.declNode = &decl;
			if (!layer->Register(name, sym))
				return QueryResult<const Type*>::Fail(QueryError::Ambiguous, "local type '" + name + "' already declared in this scope");
			return QueryResult<const Type*>::Success(owned);
		}

		if (const auto* en = dynamic_cast<const EnumDefinitionNode*>(&decl))
		{
			const Core::String name = en->GetNameToken().GetText();
			const Type* underlying = model.m_Types->Builtin(TypeKind::Int32);
			if (en->HasUnderlyingType())
			{
				auto ut = TypeResolver().ResolveTypeInternal(model, *en->GetUnderlyingType(), 0, layer, nullptr);
				if (!ut.Ok()) return QueryResult<const Type*>::Fail(ut.error, ut.message);
				underlying = ut.value;
			}
			EnumType* t = new EnumType(name, underlying);
			t->SetScoped(en->IsScoped());
			t->SetDeclNode(en);
			t->SetDeclScope(layer);
			t->SetEnclosingNames(LocalEnclosingNames(layer, en->GetNameToken().GetStartLocation()));
			int64 next = 0;
			for (const auto& m : en->GetMembers())
			{
				EnumType::MemberInfo mi;
				mi.mName = m->GetNameToken().GetText();
				if (m->HasInitializer() && m->GetInitializer())
				{
					ConstEvaluator eval(model, layer);
					auto v = eval.Evaluate(m->GetInitializer().Get(), 0, layer);
					if (!v.Ok()) { delete t; return QueryResult<const Type*>::Fail(v.error, v.message); }
					mi.m_Value = v.value.AsInt();
					next = mi.m_Value + 1;
				}
				else mi.m_Value = next++;
				t->AddMember(mi);
			}
			const Type* owned = model.m_Types->Own(t);
			model.m_EnumTypes.Push(static_cast<const EnumType*>(t));
			SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = owned; sym.definingUnit = 0; sym.declNode = &decl;
			if (!layer->Register(name, sym))
				return QueryResult<const Type*>::Fail(QueryError::Ambiguous, "local enum '" + name + "' already declared in this scope");
			// Unscoped enums inject their members as constants into the current layer (doc 09).
			if (!en->IsScoped())
				for (const auto& m : t->GetMembers())
					layer->Register(m.mName, SymbolInfo{ SymbolKind::Variable, owned, nullptr, {}, true, false, 0, &decl });
			return QueryResult<const Type*>::Success(owned);
		}

		return QueryResult<const Type*>::Fail(QueryError::NotAValue, "not a type definition: expected class/struct or enum");
	}

	QueryResult<const Type*> LoweringScopes::RegisterLocalAlias(ProgramModel& model, const AliasDefinitionNode& alias)
	{
		Scope* layer = model.m_Symbols->Current();
		if (!layer) return QueryResult<const Type*>::Fail(QueryError::NotFound, "no scope to register a local alias in");

		const Core::String name = alias.GetNameToken().GetText();
		auto at = TypeResolver().ResolveTypeInternal(model, *alias.GetAliasedType(), 0, layer, nullptr);
		if (!at.Ok()) return QueryResult<const Type*>::Fail(at.error, at.message);
		SymbolInfo sym; sym.kind = SymbolKind::Type; sym.type = at.value; sym.definingUnit = 0; sym.declNode = &alias;
		if (!layer->Register(name, sym))
			return QueryResult<const Type*>::Fail(QueryError::Ambiguous, "local alias '" + name + "' already declared in this scope");
		return QueryResult<const Type*>::Success(at.value);
	}

	QueryResult<const FunctionRecord*> LoweringScopes::RegisterLocalFunction(ProgramModel& model, const FunctionDefinitionNode& fn)
	{
		Scope* layer = model.m_Symbols->Current();
		if (!layer) return QueryResult<const FunctionRecord*>::Fail(QueryError::NotFound, "no scope to register a local function in");

		const Core::String name = fn.GetName()->GetFirstIdentifierToken().GetText();
		auto rt = TypeResolver().ResolveTypeInternal(model, *fn.GetReturnType(), 0, layer, nullptr);
		if (!rt.Ok()) return QueryResult<const FunctionRecord*>::Fail(rt.error, rt.message);
		Core::Vector<FunctionType::ParameterInfo> paramInfos;
		uint32 trailingDefaults = 0;
		for (const auto& p : fn.GetParameters())
		{
			auto pt = TypeResolver().ResolveTypeInternal(model, *p->GetType(), 0, layer, nullptr);
			if (!pt.Ok()) return QueryResult<const FunctionRecord*>::Fail(pt.error, pt.message);
			FunctionType::ParameterInfo pi;
			pi.mName = p->HasName() ? p->GetNameToken().GetText() : "arg";
			pi.m_Type = pt.value;
			paramInfos.Push(pi);
			if (p->HasDefaultValue()) trailingDefaults++;
		}
		FunctionType* sig = new FunctionType(rt.value, paramInfos);
		const Type* ownedSig = model.m_Types->Own(sig); // local: not interned (a body-local name is not global)
		FunctionRecord rec;
		rec.name = name;
		rec.type = static_cast<const FunctionType*>(ownedSig);
		rec.scope = layer;
		rec.bodyNode = fn.GetBody().Get();
		rec.declarations.Push(&fn);
		rec.definingUnit = 0;
		rec.mangledName = FunctionSymbol(layer, name, *sig);
		rec.isLocal = true; // emitted as part of its parent's tree (nestedFunctions), never top-level
		rec.minArity = paramInfos.Count() > trailingDefaults ? paramInfos.Count() - trailingDefaults : 0;
		rec.isStatic = fn.IsStatic();
		rec.isNoexcept = fn.IsNoexcept();
		for (const auto& p : fn.GetParameters()) if (p->HasName()) rec.paramNames.Push(p->GetNameToken().GetText()); // the lowerer binds param slots by name
		rec.paramIsConst = fn.ParameterConstness();
		const FunctionRecord* stored = model.m_Symbols->AddFunctionRecord(rec);
		model.m_RecordBySignature.emplace(rec.type, stored);

		SymbolInfo sym; sym.kind = SymbolKind::Function; sym.definingUnit = 0; sym.declNode = &fn;
		if (!layer->Register(name, sym, stored))
			return QueryResult<const FunctionRecord*>::Fail(QueryError::Ambiguous, "local function '" + name + "' already declared in this scope");
		return QueryResult<const FunctionRecord*>::Success(stored);
	}
} // namespace Scripting
