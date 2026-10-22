#include "Model/Builder/ModelBuilder.h"
#include "Model/Templates/MemberTemplates.h"
#include "Model/Types/ClassHierarchy.h"
#include "Model/Types/VirtualTables.h"
#include "Model/Builder/ObjectIdentity.h"
#include "Model/Builder/ModelResolver.h" // pass 2: fixed-point resolution + GAP A body scan
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"
#include "Model/ConstEvaluator.h"
#include "Model/OperatorAnnotations.h" // [group("name")] collection for reflection (plan 14)
#include <functional>

namespace Scripting
{
	void ModelBuilder::RegisterNamespace(Context& ctx, const NamespaceNode& ns)
	{
		const Core::String& name = ns.Name();
		if (name.empty() || ns.IsRoot()) return; // the implicit file root has no name

		Scope* parent = ctx.scope;
		Scope* target = nullptr;
		const SymbolInfo* existing = parent->Find(name);
		if (existing && existing->kind == SymbolKind::Namespace)
			target = const_cast<Scope*>(existing->scope); // re-enter the shared scope (open namespace)
		else
		{
			target = ctx.model->Symbols()->CreateScope(Scope::Kind::Namespace, name, parent);
			SymbolInfo info;
			info.kind = SymbolKind::Namespace;
			info.scope = target;
			info.definingUnit = 0; // a namespace is a GLOBAL entity: once any file opens it, every file may name it. Members keep their own visibility (declUnit gating), so this does not leak undeclared members - it only lets the qualified walk reach the shared scope. Gating the NAME by the first opener's unit made open namespaces invisible to sibling files that include only the shared header.
			if (!parent->Register(name, info)) ErrorAt(ctx, &ns, Redefinition(ctx, "namespace", name));
		}

		Context inner = ctx;
		inner.scope = target;
		for (const auto& child : ns.Children())
			VisitDeclaration(inner, child.Get());
	}

	void ModelBuilder::RegisterObject(Context& ctx, const ObjectDefinitionNode& obj)
	{
		const Core::String& name = obj.GetNameToken().GetText();
		if (obj.IsSpecialization())
		{
			// `template<T> struct Box<T*> { ... }`: kept with the primary template's name; instantiation picks it.
			ProgramModel::TemplateDef def;
			def.params = obj.GetTemplateDefinition().Get();
			def.objectBody = &obj;
			def.declScope = ctx.scope;
			def.pattern = obj.GetSpecializationArguments().Get();
			ctx.model->m_TypeSpecializations[name].Push(def);
			return;
		}

		// One type instance per name (doc 02): a forward declaration (`class Engine;`) may have
		// registered the name first - its shell is INCOMPLETE (IsDefined false). The definition
		// fills that same instance so references written before it see the completed type. A
		// fwd-decl AFTER the definition (or a duplicate fwd-decl) finds an already-defined type
		// and is a legal no-op, never a "redefinition" error.
		ObjectType* raw = nullptr;
		bool createdNew = false;
		if (const SymbolInfo* existing = ctx.scope->Find(name))
			if (existing->kind == SymbolKind::Type && existing->type)
				if (auto* prior = dynamic_cast<ObjectType*>(const_cast<Type*>(existing->type)))
					if (!prior->IsDefined()) raw = prior;

		if (obj.IsDeclaration() && obj.IsTemplated())
		{
			// `template<...Ts> struct Tuple;`: a primary template declared for its specializations to define.
			ProgramModel::TemplateDef def;
			def.params = obj.GetTemplateDefinition().Get();
			def.objectBody = &obj;
			def.declScope = ctx.scope;
			ctx.model->m_TypeTemplates[name].push_back(def);
			if (!ctx.scope->Find(name))
			{
				SymbolInfo info;
				info.kind = SymbolKind::Type;
				info.type = ctx.model->m_Types->Own(new ObjectType(TypeKind::Object, name));
				info.definingUnit = ctx.unitId;
				info.declNode = &obj;
				ctx.scope->Register(name, info);
			}
			return;
		}
		if (obj.IsDeclaration())
		{
			if (raw) return; // duplicate fwd-decl: the incomplete shell already exists
			const SymbolInfo* existing = ctx.scope->Find(name);
			if (existing && existing->kind == SymbolKind::Type) return; // defined elsewhere: no-op re-declaration

			raw = new ObjectType(TypeKind::Object, name); // incomplete shell - no layout until a definition
			ApplyDeclaredIdentity(*raw, obj, ctx.scope);
			Scope* classScope = ctx.model->Symbols()->CreateScope(Scope::Kind::Class, name, ctx.scope);
			classScope->objectType = raw;
			ctx.model->m_TypeScopes[raw] = classScope;
			ctx.model->m_MethodRecords[raw]; // default-construct the (empty) parallel record list
			SymbolInfo info;
				info.kind = SymbolKind::Type;
				info.type = ctx.model->m_Types->Own(raw); // take ownership ONCE
				info.definingUnit = ctx.unitId;
				info.declNode = &obj;
				{ Core::Vector<Core::String> gs; for (const auto& ga : obj.GetAnnotations()) CollectGroupAnnotations(ga.Get(), gs); raw->SetGroups(gs); } // reflection groups (plan 14)
				if (!ctx.scope->Register(name, info)) ErrorAt(ctx, &obj, Redefinition(ctx, "type", name));
				ctx.model->m_ObjectTypes.Push(static_cast<const ObjectType*>(raw));
				return;
		}

		if (!raw)
		{
			raw = new ObjectType(TypeKind::Object, name);
			const Type* owned = ctx.model->m_Types->Own(raw); // take ownership ONCE
			SymbolInfo info;
				info.kind = SymbolKind::Type;
				info.type = owned;
				info.definingUnit = ctx.unitId;
				info.declNode = &obj;
				{ Core::Vector<Core::String> gs; for (const auto& ga : obj.GetAnnotations()) CollectGroupAnnotations(ga.Get(), gs); raw->SetGroups(gs); } // reflection groups (plan 14)
				if (!ctx.scope->Register(name, info)) ErrorAt(ctx, &obj, Redefinition(ctx, "type", name));
				createdNew = true;
		}

		if (const TestRegistry::Annotation test = TestRegistry::Find(obj.GetAnnotations()); test.kind == "test")
			ctx.model->MutableTests().SetClassGroup(raw, test.group);
		raw->SetAbstract(obj.IsAbstract());
		raw->SetFinal(obj.IsFinal());
		raw->SetDefined(true); // its body is walked below - the shell (if any) is now a real type
		ApplyDeclaredIdentity(*raw, obj, ctx.scope);

		// [uuid("...")] (emitted by CBind for __declspec(uuid)/MIDL_INTERFACE): store the COM GUID so
		// __uuidof(T) can query it at compile time. Parsed here for both new types and reused shells.
		for (const auto& ann : obj.GetAnnotations())
			for (const auto& group : ann->GetBracketGroups())
				for (const auto& item : group.annotations)
					if (item && item->GetName() == "uuid" && !item->GetArguments().Empty())
						if (const auto* lit = dynamic_cast<const LiteralNode*>(item->GetArguments()[0].Get()))
						{
							ObjectType::GuidParts g;
						std::string s = lit->GetValue();
						// GUID strings are 8-4-4-4-12 hex with dashes; strip the dashes so the fixed
						// offsets below read contiguous hex. A dash at index 23 sits INSIDE Data4's
						// 12-hex tail - parsing with it present shifts every byte after Data4[1],
						// corrupting __uuidof for every COM interface (D3D12CreateDevice then fails).
						std::string h;
						for (char c : s)
							if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) h += c;
						auto hex = [](const std::string& t, size_t a, size_t n) -> uint64 {
							uint64 v = 0; for (size_t i = a; i < a + n && i < t.size(); ++i) { char c = t[i]; int d; if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10; else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else continue; v = (v << 4) | (uint64)d; } return v; };
						// GUID string: 8-4-4-4-12 hex. Data1/Data2/Data3 are stored little-endian in _GUID.
						g.m_Data1 = (uint32_t)hex(h, 0, 8);
						g.m_Data2 = (uint16_t)hex(h, 8, 4);
						g.m_Data3 = (uint16_t)hex(h, 12, 4);
						for (int i = 0; i < 8; ++i) g.m_Data4[i] = (uint8_t)(hex(h, 16 + i * 2, 2));

							raw->SetGuid(g);
						}

		// The class scope: member domain for unqualified lookup inside the type (doc 06). Created
		// for every object so nested types and `S::Member` resolve through the symbol tree. A reused
		// fwd-decl shell already has one.
		Scope* classScope = nullptr;
		auto scIt = ctx.model->m_TypeScopes.find(raw);
		if (scIt != ctx.model->m_TypeScopes.end()) classScope = const_cast<Scope*>(scIt->second);
		else
		{
			classScope = ctx.model->Symbols()->CreateScope(Scope::Kind::Class, name, ctx.scope);
			classScope->objectType = raw;
			ctx.model->m_TypeScopes[raw] = classScope;
		}
		ctx.model->m_MethodRecords[raw]; // default-construct the (empty) parallel record list

		if (createdNew) ctx.model->m_ObjectTypes.Push(static_cast<const ObjectType*>(raw));

		// Template object: record the definition for transparent instantiation (doc 05). The body
		// is NOT walked here - it is re-walked with bound parameters at the first use, so template
		// parameter names (T, K, V) never need to resolve at registration time.
		if (obj.IsTemplated())
		{
			ProgramModel::TemplateDef def;
			def.params = obj.GetTemplateDefinition().Get();
			def.objectBody = &obj;
			def.declScope = ctx.scope;
			// Same-named templates in different scopes coexist (a global `Wedge` and `Weld::Wedge`):
			// keep every def, selected by scope proximity at lookup. A flat map keyed by name alone
			// would let the last registration shadow every other same-named template program-wide.
			ctx.model->m_TypeTemplates[name].push_back(def);
			return; // no members to queue: instantiation handles them with bindings
		}

		// Non-template: queue members for pass-2 resolution. Fields, bases, and in-class method
		// declarations all resolve against the now-complete set of shells.
		Context memberCtx = ctx;
		memberCtx.scope = classScope;
		int nextRegionId = 0; // unique per anonymous union within THIS object (regions are per-object)
		for (const auto& member : obj.GetBodyMembers())
		{
			const auto* child = member.Get();
			if (!child) continue;
			switch (child->Type())
			{
			case NodeType::VariableDefinition:
				m_PendingFields.Push({ raw, static_cast<const VariableDefinitionNode*>(child), ctx.unitId });
				break;
			case NodeType::FunctionDefinition:
			{
				const auto* fn = static_cast<const FunctionDefinitionNode*>(child);
				if (!fn->IsTemplated()) m_PendingMethodDecls.Push({ raw, fn, ctx.unitId });
				else if (auto declared = MemberTemplates::Declare(*ctx.model, raw, *fn, classScope, nullptr); !declared.Ok()) ErrorAt(ctx, fn, declared.message);
				break;
			}
			case NodeType::ObjectDefinition:   RegisterObject(memberCtx, *static_cast<const ObjectDefinitionNode*>(child)); break; // nested type
			case NodeType::EnumDefinition:     RegisterEnum(memberCtx, *static_cast<const EnumDefinitionNode*>(child)); break;       // nested enum
			case NodeType::AliasDefinition:    RegisterAlias(memberCtx, *static_cast<const AliasDefinitionNode*>(child)); break;      // nested alias
			case NodeType::UnionDefinition: {
				// A NAMED union is a distinct type (RegisterUnion). An ANONYMOUS union (swizzle) has no
				// name and its lanes are flattened into the parent as region-marked fields: `v.x` / `v.r`
				// resolve to the parent, and the layout shares one byte range across all views.
			const auto& un = *static_cast<const UnionDefinitionNode*>(child);
			if (un.GetNameToken().GetText().empty())
					FlattenSwizzle(memberCtx, raw, un, nextRegionId++);
				else
					RegisterUnion(memberCtx, un);
				break;
			}
			default: break;
			}
		}
		for (const auto& b : obj.GetBaseClasses())
			m_PendingBases.Push({ raw, b.m_Type.Get(), ctx.scope, ctx.unitId }); // declScope: a base may name a sibling in the same namespace
	}

	void ModelBuilder::RegisterEnum(Context& ctx, const EnumDefinitionNode& en)
	{
		const Core::String& name = en.GetNameToken().GetText();

		// Shell with a default underlying type; the real one (and member values) resolve in pass 2.
		EnumType* raw = new EnumType(name, ctx.model->m_Types->Builtin(TypeKind::Int32));
		raw->SetScoped(en.IsScoped());
		raw->SetDeclNode(&en);
		raw->SetDeclScope(ctx.scope);
		raw->SetEnclosingNames(EnclosingNames(ctx.scope));
		ctx.model->m_Types->Own(raw);

		SymbolInfo info;
		info.kind = SymbolKind::Type;
		info.type = raw;
		info.definingUnit = ctx.unitId;
		info.declNode = &en;
		if (!ctx.scope->Register(name, info)) ErrorAt(ctx, &en, Redefinition(ctx, "enum", name));

		ctx.model->m_EnumTypes.Push(raw);
		m_PendingEnums.Push({ raw, &en, ctx.unitId, ctx.scope });

		// Unscoped enums inject their members into the enclosing scope (doc 09) so they are usable
		// as unqualified constants. The values resolve in pass 2; the names exist from here on.
		if (!en.IsScoped())
			for (const auto& m : en.GetMembers())
				ctx.scope->Register(m->GetNameToken().GetText(),
					SymbolInfo{ SymbolKind::Variable, raw, nullptr, {}, true, false, ctx.unitId, &en });
	}

	void ModelBuilder::RegisterAlias(Context& ctx, const AliasDefinitionNode& alias)
	{
		const Core::String& name = alias.GetNameToken().GetText();

		if (alias.IsTemplated())
		{
			// Alias template: transparent at use - `TBox<int32>` resolves to the bound target.
			ProgramModel::TemplateDef def;
			def.params = alias.GetTemplateDefinition().Get();
			def.aliasTarget = alias.GetAliasedType().Get();
			def.isAlias = true;
			def.declScope = ctx.scope;
			ctx.model->m_TypeTemplates[name].push_back(def); // coexist with same-named templates in other scopes
			SymbolInfo info;
			info.kind = SymbolKind::Type;
			info.definingUnit = ctx.unitId;
			ctx.scope->Register(name, info);
			return;
		}

		// Shell with a null target; the real target resolves in pass 2 (forward refs work).
		AliasType* raw = new AliasType(name, nullptr);
		ctx.model->m_Types->Own(raw);

		SymbolInfo info;
		info.kind = SymbolKind::Type;
		info.type = raw;
		info.definingUnit = ctx.unitId;
		info.declNode = &alias;
		const AliasType* repeats = nullptr;
		if (!ctx.scope->Register(name, info))
		{
			repeats = EarlierAlias(*ctx.scope, name);
			if (!repeats) ErrorAt(ctx, &alias, Redefinition(ctx, "alias", name));
		}
		// scope: the target resolves in the alias's own namespace
		m_PendingAliases.Push({ raw, alias.GetAliasedType().Get(), ctx.unitId, ctx.scope, repeats, &alias });
	}

	// As in C++, an alias may be declared again in its scope when it names the same type: two C headers' bindings
	// in one file each declare `size_t`.
	const AliasType* ModelBuilder::EarlierAlias(const Scope& scope, const Core::String& name)
	{
		for (const auto& entry : scope.Entries(name))
			if (entry->kind == SymbolKind::Type && entry->type && entry->type->GetKind() == TypeKind::Alias)
				return static_cast<const AliasType*>(entry->type);
		return nullptr;
	}
} // namespace Scripting
