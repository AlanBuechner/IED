#include "Model/TypeResolver.h"
#include "Model/Types/ReturnLists.h"
#include "Model/Templates/Packs.h"
#include "Model/OverloadResolution.h"
#include "Model/ConstEvaluator.h"
#include "Model/Types/SwizzleType.h"
#include "Model/TemplateInstantiator.h" // specialization instantiation (doc 05)
#include "Model/Types/ObjectType.h"     // nested-type prefix descent (dynamic_cast to ObjectType)
#include "Model/TypeHandle.h"           // UnwrapToValue brick (qualified walk descends into types)
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Declarations/FunPtrTypeNode.h"
#include <string>

namespace Scripting
{
	// ---------------------------------------------------------------------------
	// TYPE RESOLUTION (doc 04): one entry point for every type position in the AST.
	// ---------------------------------------------------------------------------
	QueryResult<const Type*> TypeResolver::ResolveType(const ProgramModel& model, const QualTypeNode& node, uint32 unitId) const
	{
		return ResolveTypeInternal(model, node, unitId, nullptr, model.m_Symbols->GetRoot());
	}

	QueryResult<const Type*> TypeResolver::ResolveTypeInContext(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, const Scope* context) const
	{
		// BUILD-TIME resolution is program-wide (unitId 0 = sees everything). The model must be
		// complete and valid for the whole program regardless of include order (doc 02
		// order-independence; doc 03 one program-wide model): a header that names a type defined in
		// a sibling header it does not itself include still resolves, because every declaration is
		// registered before any type is resolved. Per-unit include-closure visibility remains a
		// QUERY-TIME concept (LSP/DAP "what does THIS unit see?") via the public ResolveType/QueryType
		// that take a real unitId. Cross-file duplicate type names cannot be silently mis-resolved:
		// they are rejected at registration in pass 1, so only one definition of a name ever exists.
		// The unit's project still bounds what it may name (D-59).
		ProgramModel::ProjectReach reach(model, unitId);
		return ResolveTypeInternal(model, node, 0, nullptr, context);
	}

	QueryResult<const Type*> TypeResolver::ResolveTypeInUnitContext(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, const Scope* context) const
	{
		// Like ResolveType (unit include-closure visibility), but with the function's enclosing scope as
		// the lookup context so an unqualified name declared in that namespace resolves. Falls back to
		// root when no context is supplied - identical to plain ResolveType.
		const Scope* ctx = context ? context : model.m_Symbols->GetRoot();
		return ResolveTypeInternal(model, node, unitId, nullptr, ctx);
	}

	// Where an unqualified name is looked up past the template bindings: the asking context, else the bindings'
	// own chain (an instance's sits on its template's declaring scope), else the root.
	const Scope* TypeResolver::UnqualifiedLookupStart(const ProgramModel& model, const Scope* bindingScope, const Scope* context)
	{
		if (context) return context;
		return bindingScope ? bindingScope : model.m_Symbols->GetRoot();
	}

	// A segment with template arguments: a BARE template name resolves in the asking context (the alias's own
	// namespace), exactly like an ordinary unqualified name - `alias IWedge = Wedge<int32>` inside `namespace Weld`
	// binds Weld's Wedge, not a same-named global; a qualified one in the scope the path reached. The arguments
	// always resolve where they are written (BUG-178).
	QueryResult<const Type*> TypeResolver::InstantiateSegment(const ProgramModel& model, const NamespaceQualifiedNameNode::Segment& segment, const Scope* templateFrom,
		uint32 unitId, Scope* bindingScope, const Scope* context) const
	{
		return TemplateInstantiator().InstantiateTypeTemplate(model, segment.nameToken.GetText(), templateFrom, *segment.templateArgs, unitId,
			bindingScope, context);
	}

	QueryResult<const Type*> TypeResolver::ResolveTypeInternal(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, Scope* bindingScope, const Scope* context) const
	{
		if (node.IsReturnList()) return ResolveReturnList(model, node, unitId, bindingScope, context);
		// `funcptr<R(A)>` / `func<R(A)>`, resolved recursively, then the layers written after it (`[2]`, `*`).
		if (node.FuncPtrType())
		{
			const auto* fp = dynamic_cast<const FunPtrTypeNode*>(node.FuncPtrType().Get());
			if (!fp) return QueryResult<const Type*>::Fail(QueryError::NotFound, "invalid funcptr type");
			auto ret = ResolveTypeInternal(model, *fp->GetReturnType(), unitId, bindingScope, context);
			if (!ret.Ok()) return ret;
			Core::Vector<const Type*> params;
			for (const auto& p : fp->GetParameters())
			{
				auto pt = ResolveTypeInternal(model, *p->GetType(), unitId, bindingScope, context);
				if (!pt.Ok()) return pt;
				params.Push(pt.value);
			}
			const Type* signature = fp->IsFunc() ? model.m_Types->Func(ret.value, params) : model.m_Types->FunPtr(ret.value, params);
			return ApplyIndirections(model, signature, node, unitId, bindingScope, context);
		}

		const auto* base = node.BaseName().Get();
		if (!base || base->SegmentCount() == 0)
			return QueryResult<const Type*>::Fail(QueryError::NotFound, "type has no name");

		// Walk the qualified segments. bindingScope (instantiation context) is checked FIRST so a
		// template parameter name binds to its concrete type before any global lookup - this is the
		// whole of template instantiation: fake aliases in a scope, nothing else. Its parent chain
		// carries outer bindings and then globals, so unbound names fall through naturally.
		const Scope* from = model.m_Symbols->GetRoot();
		Core::String fullPath;
		for (uint32 i = 0; i < base->SegmentCount(); ++i)
		{
			const auto& seg = base->GetSegments()[i];
			const Core::String& name = seg.nameToken.GetText();
			if (!fullPath.empty()) fullPath += "::";
			fullPath += name;

			bool isFinal = (i == base->SegmentCount() - 1);

			// Template specialization: instantiate transparently (doc 05) before any symbol lookup.
			if (isFinal && seg.templateArgs)
			{
				// `__init_list<T>` is a BUILT-IN alias template (doc 05): it resolves to an ordinary
				// InitListType with no user definition. Checked before the symbol table so it works
				// in every unit without any declaration.
				if (fullPath == "__init_list" && seg.templateArgs->ArgumentCount() == 1)
				{
					if (const auto* elemNode = dynamic_cast<const QualTypeNode*>(seg.templateArgs->GetArguments()[0].Get()))
					{
						auto elem = ResolveTypeInternal(model, *elemNode, unitId, bindingScope, context);
						if (!elem.Ok()) return QueryResult<const Type*>::Fail(elem.error, elem.message);
						const Type* il = model.m_Types->InitList(elem.value);
						if (!il) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "invalid __init_list element type");
						return ApplyIndirections(model, il, node, unitId, bindingScope, context);
					}
					return QueryResult<const Type*>::Fail(QueryError::NotAValue, "__init_list takes one type argument");
				}
				auto inst = InstantiateSegment(model, seg, i == 0 ? UnqualifiedLookupStart(model, bindingScope, context) : from, unitId, bindingScope, context);
				if (!inst.Ok()) return inst;
				return ApplyIndirections(model, inst.value, node, unitId, bindingScope, context);
			}
			// `Outer<float>::Inner`: an instance's own class layer holds its member types (BUG-179).
			if (seg.templateArgs)
			{
				auto inst = InstantiateSegment(model, seg, i == 0 ? UnqualifiedLookupStart(model, bindingScope, context) : from, unitId, bindingScope, context);
				if (!inst.Ok()) return inst;
				const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(inst.value));
				const Scope* members = obj ? model.TypeScope(obj) : nullptr;
				if (!members) return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a class: " + fullPath);
				from = members;
				continue;
			}

			const SymbolInfo* info = nullptr;
			if (bindingScope && i == 0)
			{
				const SymbolInfo* bound = bindingScope->Find(name);
				if (bound && bound->kind == SymbolKind::Type) info = bound;
			}
			// A type declared in the body being lowered (doc 06) is visible from this lowering
			// position: check the RAII chain before falling through to globals.
			if (!info && i == 0)
			{
				const SymbolInfo* local = model.FindLocal(name);
				if (local && local->kind == SymbolKind::Type) info = local;
			}
			if (info && info->IsPack())
				return QueryResult<const Type*>::Fail(QueryError::NotAValue, "the pack '" + name + "' must be expanded with '...'");
			const Scope* lookupFrom = i == 0 ? UnqualifiedLookupStart(model, bindingScope, context) : from;
			if (!info)
			{
				info = model.m_Symbols->FindVisible(lookupFrom, name, unitId);
				if (info)
					if (Core::String why = model.OutOfProjectReach(*info, name); !why.empty())
						return QueryResult<const Type*>::Fail(QueryError::NotVisible, why);
			}
			if (!info)
			{
				// Builtins are not symbols - they are interned in the type store (doc 03). A missing
				// symbol falls back to the builtin table; a built-in can only close a qualified path.
				const Type* b = model.m_Types->FindBuiltin(name);
				if (b && isFinal)
					return ApplyIndirections(model, b, node, unitId, bindingScope, context);
				return QueryResult<const Type*>::Fail(QueryError::NotFound, "type not found: " + name);
			}

		if (info->kind == SymbolKind::Namespace)
			{
				if (isFinal) return QueryResult<const Type*>::Fail(QueryError::NotAValue, "namespace is not a type: " + name);
				from = info->scope; // descend; the next segment resolves inside it
				continue;
			}
		if (!isFinal)
		{
			// A non-final segment that is a TYPE is a nested-type prefix (`Outer::Inner`): descend into
			// that type's class scope so the next segment (the nested type name) resolves inside it.
			// Without this, `Outer::Inner in;` fails with "not a namespace path: Outer".
			if (info->kind == SymbolKind::Type && info->type)
			{
				const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(info->type));
				if (obj)
				{
					const Scope* ts = model.TypeScope(obj);
					if (ts) { from = ts; continue; }
				}
			}
			return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a namespace path: " + name);
		}
			if (info->kind != SymbolKind::Type)
				return QueryResult<const Type*>::Fail(QueryError::NotAValue, "not a type: " + name);
			// An alias is transparent: the type it names, never the alias itself (BUG-187). One whose target is
			// not resolved yet fails, so what names it waits for the next round.
			const Type* named = info->type;
			while (named && named->GetKind() == TypeKind::Alias) named = static_cast<const AliasType*>(named)->GetAliasedType();
			if (!named) return QueryResult<const Type*>::Fail(QueryError::NotFound, "the alias '" + name + "' is not resolved yet");
			return ApplyIndirections(model, named, node, unitId, bindingScope, context);
		}
		return QueryResult<const Type*>::Fail(QueryError::NotFound, "empty qualified name");
	}

	QueryResult<const Type*> TypeResolver::ResolveReturnList(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, Scope* bindingScope,
		const Scope* context) const
	{
		std::vector<const Type*> elements;
		for (const auto& element : node.GetReturnList())
		{
			if (!element->ExpandsPack())
			{
				auto t = ResolveTypeInternal(model, *element, unitId, bindingScope, context);
				if (!t.Ok()) return t;
				elements.push_back(t.value);
				continue;
			}
			// `[Ts...]`: the element once per element of the packs it names.
			auto packs = Packs::In(model, element.Get(), bindingScope);
			if (!packs.Ok()) return QueryResult<const Type*>::Fail(packs.error, packs.message);
			for (uint32 i = 0; i < Packs::Length(packs.value); ++i)
			{
				auto t = ResolveTypeInternal(model, *element, unitId, Packs::ElementScope(model, packs.value, i, bindingScope), context);
				if (!t.Ok()) return t;
				elements.push_back(t.value);
			}
		}
		if (elements.size() < 2)
			return QueryResult<const Type*>::Fail(QueryError::NotAValue, "a return list has at least two values, but '" + node.ToString() +
				"' has " + std::to_string(elements.size()) + ": return the value itself");
		auto list = ReturnLists::Of(model, elements);
		if (!list.Ok()) return QueryResult<const Type*>::Fail(list.error, list.message);
		return QueryResult<const Type*>::Success(list.value);
	}

	namespace
	{
		// An array extent is an integer constant of at least 1 (BUG-121, BUG-122).
		QueryResult<uint64> ArrayExtent(const ConstantValue& value, const AstNode& expression)
		{
			const Core::String text = expression.ToString();
			if (value.kind == ConstantValue::Kind::Float64)
				return QueryResult<uint64>::Fail(QueryError::NotAValue,
					"an array extent is an integer, but '" + text + "' is a floating-point value");
			if (value.kind != ConstantValue::Kind::Int64 && value.kind != ConstantValue::Kind::Bool)
				return QueryResult<uint64>::Fail(QueryError::NotAValue, "an array extent is an integer, but '" + text + "' is not");
			if (value.AsInt() < 1)
				return QueryResult<uint64>::Fail(QueryError::NotAValue,
					"an array extent is at least 1, but '" + text + "' is " + std::to_string(value.AsInt()));
			return QueryResult<uint64>::Success(static_cast<uint64>(value.AsInt()));
		}
	}

	Core::String TypeResolver::PackedWithoutLanes(const QualTypeNode& node, const Type* base)
	{
		const auto& layers = node.GetIndirections();
		if (!layers.Empty() && layers[0].indirectionToken != TokenType::OpenBracket && node.HasArrayLayer())
			return "'" + node.ToString() + "' is not a vector: the element of a packed vector is a number, not a pointer or a reference";
		return "'packed' needs a lane count: write 'packed " + base->GetName() + "[N]' for a vector value (a lane view, 'packed " +
			base->GetName() + " x, y;', goes inside a union)";
	}

	QueryResult<const Type*> TypeResolver::ApplyIndirections(const ProgramModel& model, const Type* baseType, const QualTypeNode& node, uint32 unitId,
		Scope* bindingScope, const Scope* context) const
	{
		// Each layer's const qualifies that layer's value (D-30): the leading const the base, a const before
		// a `*` that pointer. A pointer or reference built over a const value points at const.
		const auto& layers = node.GetIndirections();
		const Type* current = baseType;
		bool valueConst = node.IsLeadingConst();
		// `packed T[N]` (D-36): the first layer, an array, is one SIMD vector.
		if (node.IsPacked() && (layers.Empty() || !layers[0].arraySizeExpr))
			return QueryResult<const Type*>::Fail(QueryError::NotAValue, PackedWithoutLanes(node, TypeHandle::UnwrapToValue(current)));
		for (uint32 i = 0; i < layers.Count(); ++i)
		{
			const IndirectionLayer& layer = layers[i];
			if (layer.arraySizeExpr && i == 0 && node.IsPacked())
			{
				ConstEvaluator eval(model, context);
				auto size = eval.Evaluate(layer.arraySizeExpr.Get(), unitId, bindingScope);
				if (!size.Ok())
					return QueryResult<const Type*>::Fail(size.error, "the lane count '" + layer.arraySizeExpr->ToString() +
						"' is not a constant expression: " + size.message, layer.arraySizeExpr->startLocation);
				const Type* element = TypeHandle::UnwrapToValue(current);
				if (Core::String why = SwizzleType::WhyNotAVector(element, static_cast<uint64>(size.value.AsInt()), 2); !why.empty())
					return QueryResult<const Type*>::Fail(QueryError::NotAValue, why);
				current = model.m_Types->Swizzle(element, static_cast<uint32>(size.value.AsInt()));
				continue;
			}
			if (layer.arraySizeExpr)
			{
				// The size may name a template non-type parameter (`int32[N]`): fold it against the
				// instantiation's binding layer so N resolves to its bound value.
				ConstEvaluator eval(model, context);
				auto size = eval.Evaluate(layer.arraySizeExpr.Get(), unitId, bindingScope);
				if (!size.Ok())
					return QueryResult<const Type*>::Fail(size.error, "the array size '" + layer.arraySizeExpr->ToString() +
						"' is not a constant expression: " + size.message, layer.arraySizeExpr->startLocation);
				auto extent = ArrayExtent(size.value, *layer.arraySizeExpr);
				if (!extent.Ok()) return QueryResult<const Type*>::Fail(extent.error, extent.message);
				current = model.m_Types->Array(current, extent.value);
				if (Core::String why = ObjectSizeProblem(*current); !why.empty())
					return QueryResult<const Type*>::Fail(QueryError::NotAValue, why, layer.arraySizeExpr->startLocation);
				valueConst = valueConst || layer.isConst; // an array of const elements is a const array
			}
			else if (layer.indirectionToken == TokenType::OpenBracket)
			{
				// D-33: an array always has an extent; a run of unknown length is a pointer or a Vector.
				const Core::String element = current->GetName();
				return QueryResult<const Type*>::Fail(QueryError::NotAValue, "'" + element + "[]' is not a type: an array needs an extent; use '" +
					element + "*' or 'Core::Array<" + element + ">'");
			}
			else if (layer.indirectionToken == TokenType::Star)
			{
				current = model.m_Types->Pointer(current, valueConst);
				valueConst = layer.isConst;
			}
			else if (layer.indirectionToken == TokenType::Ampersand)
			{
				current = model.m_Types->Reference(current, valueConst);
				valueConst = false;
			}
		}
		return QueryResult<const Type*>::Success(current);
	}

	QueryResult<const Type*> TypeResolver::QueryType(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* scopeContext) const
	{
		if (!model.IsValid()) return QueryResult<const Type*>::Fail(QueryError::NotFound, "model not built");
		// A type declared in the body being lowered (doc 06) is visible from this lowering position:
		// check the RAII chain before falling through to globals.
		const SymbolInfo* local = model.FindLocal(name);
		if (local && local->IsPack())
			return QueryResult<const Type*>::Fail(QueryError::NotAValue, "the pack '" + name + "' must be expanded with '...'");
		if (local && local->kind == SymbolKind::Type) return QueryResult<const Type*>::Success(local->type);
		// Unqualified name with a scope context: resolve from that scope (walks up to globals), so a
		// type declared in the function's namespace is found. Qualified names always walk from root.
		const SymbolInfo* info = nullptr;
		if (name.find("::") == Core::String::npos && scopeContext)
			info = model.m_Symbols->FindVisible(scopeContext, name, unitId);
		else
			info = FindVisibleQualified(model, name, unitId);
		if (info && info->kind == SymbolKind::Type)
		{
			const Type* t = info->type;
			while (t && t->GetKind() == TypeKind::Alias) // aliases are transparent: the SAME instance
				t = static_cast<const AliasType*>(t)->GetAliasedType();
			return QueryResult<const Type*>::Success(t);
		}
		if (info)
			return QueryResult<const Type*>::Fail(QueryError::NotFound, "not a type: " + name); // a function name is NotFound as a type
		// Builtins are not symbols - they are interned in the type store (doc 03). A missing symbol
		// falls back to the builtin table so QueryType("int32") matches ResolveType(int32).
		const Type* b = model.m_Types->FindBuiltin(name);
		if (b) return QueryResult<const Type*>::Success(b);
		return QueryResult<const Type*>::Fail(QueryError::NotFound, "type not found: " + name);
	}

	// ---------------------------------------------------------------------------
	// VISIBILITY-AWARE QUALIFIED WALK - every name query goes through this.
	// ---------------------------------------------------------------------------
	const SymbolInfo* TypeResolver::FindVisibleQualified(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* context) const
	{
		// A name resolves from the asking scope (namespace/class/block) outward to the root, as in C++: a bare
		// `Deep` inside `namespace NsOuter::NsInner` finds Deep declared there, and `Inner::K` finds the Inner
		// nested in an enclosing namespace. A qualified name the context does not lead to starts at the root.
		const Scope* root = model.m_Symbols->GetRoot();
		if (const SymbolInfo* found = WalkQualified(model, name, unitId, context ? context : root)) return found;
		return context && context != root && name.find("::") != Core::String::npos ? WalkQualified(model, name, unitId, root) : nullptr;
	}

	const SymbolInfo* TypeResolver::WalkQualified(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* scope) const
	{
		size_t start = 0;
		while (true)
		{
			size_t colon = name.find("::", start);
			Core::String segment = (colon == Core::String::npos) ? name.substr(start) : name.substr(start, colon - start);
			bool isFinal = (colon == Core::String::npos);

			const SymbolInfo* info = model.m_Symbols->FindVisible(scope, segment, unitId);
			if (!info) return nullptr;
			if (isFinal) return info;

			// Descend: into a namespace scope, or into a type's class scope (nested types).
			if (info->kind == SymbolKind::Namespace && info->scope) { scope = info->scope; start = colon + 2; continue; }
			if (info->kind == SymbolKind::Type && info->type)
			{
				const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(info->type));
				if (obj)
				{
					auto it = model.m_TypeScopes.find(obj);
					if (it != model.m_TypeScopes.end()) { scope = it->second; start = colon + 2; continue; }
				}
			}
			return nullptr; // cannot descend further
		}
	}
} // namespace Scripting
