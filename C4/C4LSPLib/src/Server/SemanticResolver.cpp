#include "Server/SemanticResolver.h"
#include "Model/TypeHandle.h" // TypeHandle::UnwrapToValue (alias/reference -> value type)
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"

namespace C4LSP::Server
{
	namespace
	{
		int FindFunctionIndex(const Core::Vector<const Scripting::AstNode*>& path)
		{
			for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
				if (path[i]->Type() == Scripting::NodeType::FunctionDefinition)
					return i;
			return -1;
		}

		uint32 UnitIdOf(const Scripting::AstNode& node) { return node.startLocation.sourceId; }

		const Scripting::ObjectType* UnwrapObject(const Scripting::Type* t)
		{
			return dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(t));
		}

		/// Faithful replica of ProgramModel::FindVisibleQualified (which is private), built from the model's
		/// PUBLIC verbs so the LSP resolves names exactly as the compiler does - no duplicated resolution logic.
		/// A qualified name starts at root; an unqualified name starts at `context` (the enclosing namespace/class)
		/// and walks up. Each segment: a namespace descends into its scope, a type into its class scope (nested types).
		const Scripting::SymbolInfo* FindVisibleQualified(const Scripting::ProgramModel& model, const std::string& name, uint32 unitId, const Scripting::Scope* context)
		{
			const bool qualified = name.find("::") != std::string::npos;
			const Scripting::Scope* scope = (!qualified && context) ? context : model.Symbols()->GetRoot();
			size_t start = 0;
			while (true)
			{
				size_t colon = name.find("::", start);
				std::string segment = (colon == std::string::npos) ? name.substr(start) : name.substr(start, colon - start);
				const bool isFinal = (colon == std::string::npos);

				const Scripting::SymbolInfo* info = model.Symbols()->FindVisible(scope, segment, unitId);
				if (!info) return nullptr;
				if (isFinal) return info;

				if (info->kind == Scripting::SymbolKind::Namespace && info->scope) { scope = info->scope; start = colon + 2; continue; }
				if (info->kind == Scripting::SymbolKind::Type && info->type)
				{
					const auto* obj = dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(info->type));
					if (obj)
					{
						const Scripting::Scope* ts = model.TypeScope(obj);
						if (ts) { scope = ts; start = colon + 2; continue; }
					}
				}
				return nullptr; // cannot descend further
			}
		}

		/// The null-context fast path: a qualified name always walks from root, so the model's public
		/// QuerySymbol verb (which wraps FindVisibleQualified) is exact for it.
		const Scripting::SymbolInfo* QuerySym(const Scripting::ProgramModel& model, const std::string& name, uint32 unitId)
		{
			auto r = model.QuerySymbol(name, unitId);
			return r.Ok() ? r.value : nullptr;
		}

		std::string JoinSegments(const Scripting::NamespaceQualifiedNameNode& nqn)
		{
			std::string out;
			for (uint32 i = 0; i < nqn.SegmentCount(); ++i)
				out += (i ? "::" : "") + std::string(nqn.GetSegments()[i].nameToken.GetText());
			return out;
		}

		/// The qualified path of a StaticAccess chain (`A::B::C`) outermost-first, or "" if unjoinable.
		std::string StaticAccessPath(const Scripting::AstNode* node)
		{
			std::vector<std::string> segments; // innermost first
			const Scripting::AstNode* cur = node;
			while (cur && cur->Type() == Scripting::NodeType::StaticAccess)
			{
				const auto& inner = static_cast<const Scripting::StaticAccessNode&>(*cur);
				segments.push_back(inner.GetMemberNameToken().GetText());
				cur = inner.GetBaseType().Get();
			}
			std::string prefix;
			if (cur && cur->Type() == Scripting::NodeType::IdentifierExpression)
			{
				segments.push_back(static_cast<const Scripting::IdentifierExpressionNode&>(*cur).GetName());
				std::reverse(segments.begin(), segments.end()); // outermost first
				for (size_t i = 0; i < segments.size(); ++i)
					prefix += (i ? "::" : "") + segments[i];
			}
			else if (const auto* nqn = dynamic_cast<const Scripting::NamespaceQualifiedNameNode*>(cur))
			{
				for (uint32 i = 0; i < nqn->SegmentCount(); ++i)
					prefix += (prefix.empty() ? "" : "::") + std::string(nqn->GetSegments()[i].nameToken.GetText());
				for (const auto& s : segments)
					prefix += "::" + s;
			}
			return prefix;
		}
	}

	const Scripting::ObjectType* SemanticResolver::EnclosingObject(const Core::Vector<const Scripting::AstNode*>& path) const
	{
		int oi = -1;
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
			if (path[i]->Type() == Scripting::NodeType::ObjectDefinition)
			{
				oi = i;
				break;
			}
		if (oi < 0)
			return nullptr;

		const auto& objNode = static_cast<const Scripting::ObjectDefinitionNode&>(*path[oi]);
		std::string qualified = objNode.GetNameToken().GetText();
		std::vector<std::string> namespaces; // innermost first, for the prefix join below
		for (int i = oi - 1; i >= 0; --i)
			if (path[i]->Type() == Scripting::NodeType::Namespace)
				namespaces.push_back(static_cast<const Scripting::NamespaceNode&>(*path[i]).Name());

		for (size_t depth = 0; depth <= namespaces.size(); ++depth)
		{
			std::string candidate = qualified;
			for (size_t k = 0; k < depth; ++k) // innermost-first, then reverse to outermost-first
				candidate = namespaces[k] + "::" + candidate;
			if (const Scripting::SymbolInfo* info = QuerySym(m_Model, candidate, UnitIdOf(objNode)))
				if (info->kind == Scripting::SymbolKind::Type && info->type)
					return UnwrapObject(info->type);
		}
		return nullptr;
	}

	SemanticResolver::Ctx SemanticResolver::ContextFor(const Core::Vector<const Scripting::AstNode*>& path) const
	{
		Ctx ctx;
		int fi = FindFunctionIndex(path);
		if (fi < 0)
			return ctx;
		const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(*path[fi]);
		ctx.unitId = UnitIdOf(fn);
		if (!fn.GetName())
			return ctx;

		std::string name = JoinSegments(*fn.GetName());
		const Scripting::ObjectType* receiver = EnclosingObject(path);
		const Scripting::FunctionRecord* rec = nullptr;
		auto qctx = [&](const Scripting::Scope* scope) {
			Scripting::LookupContext c;
			c.unitId = ctx.unitId;
			c.enclosingType = receiver;
			c.scope = scope;
			return c;
		};
		// A method needs its class context to resolve; a free function resolves from root. Try both so the
		// record we get is the one HIR would lower - containingType and scope then match the compiler's.
		if (m_Model.QueryFunction(name, nullptr, nullptr, Core::Vector<const Scripting::Type*>(), qctx(nullptr), &rec).Ok())
			ctx.enclosingType = rec->containingType ? rec->containingType : receiver;
		else if (receiver && m_Model.QueryFunction(name, receiver, nullptr, Core::Vector<const Scripting::Type*>(), qctx(nullptr), &rec).Ok())
			ctx.enclosingType = rec->containingType ? rec->containingType : receiver;
		if (rec)
			ctx.scope = rec->scope; // the function's enclosing namespace/class - unqualified lookup context
		return ctx;
	}

	const Scripting::SymbolInfo* SemanticResolver::Lookup(const std::string& name, uint32 unitId,
		const Core::Vector<const Scripting::AstNode*>* path) const
	{
		if (!m_Model.IsValid())
			return nullptr;
		const Scripting::Scope* ctx = nullptr; // qualified names always walk from root (mirrors FindVisibleQualified)
		if (path && name.find("::") == std::string::npos)
			ctx = ContextFor(*path).scope;
		return FindVisibleQualified(m_Model, name, unitId, ctx);
	}

	bool SemanticResolver::ResolveLocal(const std::string& name, const Core::Vector<const Scripting::AstNode*>& path,
		const Scripting::AstNode*& declOut, const Scripting::Type*& typeOut) const
	{
		declOut = nullptr;
		typeOut = nullptr;
		int fi = FindFunctionIndex(path);

		// Parameters: visible from the function node down. Stop at the function boundary so a same-named
		// local in an outer (sibling) function never leaks in - the exact rule HIR's scope chain enforces.
		if (fi >= 0)
		{
			const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(*path[fi]);
			for (const auto& param : fn.GetParameters())
				if (param && param->HasName() && param->GetNameToken().GetText() == name)
				{
					declOut = param.Get();
					if (param->GetType())
						typeOut = ResolveQualType(*param->GetType(), path);
					return true;
				}
		}

		// Locals: innermost CodeBlock first, outward to the function boundary. A for-loop's init clause
		// declares its variable in the loop's scope (visible in condition / increment / body).
		for (int i = static_cast<int>(path.Count()) - 1; i > fi; --i)
		{
			const Scripting::AstNode* node = path[i];
			if (node->Type() == Scripting::NodeType::CodeBlock)
			{
				const auto& block = static_cast<const Scripting::CodeBlockNode&>(*node);
				for (const auto& stmt : block.GetStatements())
					if (stmt && stmt->Type() == Scripting::NodeType::VariableDefinition)
					{
						const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(*stmt.Get());
						for (const auto& d : var.GetDeclarators())
							if (d.m_NameToken.GetText() == name)
							{
								declOut = stmt.Get();
								if (var.GetType())
									typeOut = ResolveQualType(*var.GetType(), path);
								return true; // innermost matching local wins
							}
					}
			}
			else if (node->Type() == Scripting::NodeType::ForLoop)
			{
				const auto& loop = static_cast<const Scripting::ForLoopNode&>(*node);
				if (loop.IsRangeBased())
				{
					if (loop.GetRangeVarNameToken().GetText() == name)
					{
						declOut = node; // the range variable is declared by the for statement itself
						if (loop.GetInitClause() && loop.GetInitClause()->Type() == Scripting::NodeType::QualType)
							typeOut = ResolveQualType(*static_cast<const Scripting::QualTypeNode*>(loop.GetInitClause().Get()), path);
						return true;
					}
				}
				else if (loop.GetInitClause() && loop.GetInitClause()->Type() == Scripting::NodeType::VariableDefinition)
				{
					const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(*loop.GetInitClause().Get());
					for (const auto& d : var.GetDeclarators())
						if (d.m_NameToken.GetText() == name)
						{
							declOut = loop.GetInitClause().Get();
							if (var.GetType())
								typeOut = ResolveQualType(*var.GetType(), path);
							return true;
						}
				}
			}
		}
		return false;
	}

	NodeResolution SemanticResolver::ResolveIdentifier(const std::string& name, const Core::Vector<const Scripting::AstNode*>& path) const
	{
		if (name.empty() || name == "this")
			return {};

		// 1) Lexical locals + parameters (HIR's FindLocal equivalent, derived from the AST path).
		const Scripting::AstNode* localDecl = nullptr;
		const Scripting::Type* localType = nullptr;
		if (ResolveLocal(name, path, localDecl, localType))
			return { localType, localDecl, false };

		// 2) The model's own variable query: enclosing-type field (implicit this), then global. Build-time
		//    visibility (unitId 0) so a name defined in any file is found - the same rule the compiler uses
		//    when it builds the program-wide model, and what an editor needs for completion/navigation.
		Ctx ctx = ContextFor(path);
		Scripting::LookupContext qctx;
		qctx.unitId = 0;
		qctx.enclosingType = ctx.enclosingType;
		qctx.scope = ctx.scope;
		auto var = m_Model.QueryVariable(name, nullptr, qctx);
		if (var.Ok())
		{
			NodeResolution res;
			res.type = var.value.type;
			res.declaration = var.value.symbol ? var.value.symbol->declNode : nullptr;
			if (!res.declaration && ctx.enclosingType) // a field: locate its declaring node on the type chain
			{
				for (const Scripting::ObjectType* t = ctx.enclosingType; t; )
				{
					if (const auto* f = t->GetFieldByName(name)) { res.declaration = f->m_DeclaringNode; break; }
					t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr;
				}
			}
			return res;
		}

		// 3) A function name in expression position (call callee, decay, or bare reference).
		auto fn = m_Model.QueryFunction(name, nullptr, nullptr, Core::Vector<const Scripting::Type*>(), qctx);
		if (fn.Ok())
		{
			NodeResolution res;
			res.isFunction = true;
			res.type = fn.value;
			const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, name, 0, ctx.scope);
			res.declaration = info ? info->declNode : nullptr;
			return res;
		}
		return {};
	}

	NodeResolution SemanticResolver::ResolveReference(const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path) const
	{
		auto cached = m_RefCache.find(&node);
		if (cached != m_RefCache.end())
			return cached->second;

		NodeResolution res;
		uint32 unitId = UnitIdOf(node);

		switch (node.Type())
		{
		case Scripting::NodeType::IdentifierExpression: {
			res = ResolveIdentifier(static_cast<const Scripting::IdentifierExpressionNode&>(node).GetName(), path);
			break;
		}
		case Scripting::NodeType::MemberAccess: {
			const auto& ma = static_cast<const Scripting::MemberAccessNode&>(node);
			if (!ma.GetBaseObject())
				break;
			const std::string member = ma.GetMemberNameToken().GetText();
			const Scripting::AstNode* base = ma.GetBaseObject().Get();

			// `Type.member` / `Ns::Type.member`: the base names a TYPE (static member), checked first -
			// exactly HIR's ResolveMemberAccess ordering.
			std::string basePath;
			if (base->Type() == Scripting::NodeType::IdentifierExpression)
				basePath = static_cast<const Scripting::IdentifierExpressionNode&>(*base).GetName();
			else if (base->Type() == Scripting::NodeType::StaticAccess || base->Type() == Scripting::NodeType::NamespaceQualifiedName)
				basePath = StaticAccessPath(base);

			const Scripting::SymbolInfo* owner = basePath.empty() ? nullptr : QuerySym(m_Model, basePath, unitId);
			const Scripting::ObjectType* obj = (owner && owner->kind == Scripting::SymbolKind::Type && owner->type) ? UnwrapObject(owner->type) : nullptr;

			if (!obj) // base is an expression: resolve its value type, then query the member on it
			{
				NodeResolution baseRes = ResolveReference(*base, path);
				const Scripting::ObjectType* baseObj = baseRes.type ? UnwrapObject(baseRes.type) : nullptr;
				if (!baseObj)
					break;
				auto mb = m_Model.QueryMember(static_cast<const Scripting::Type*>(baseObj), member);
				if (!mb.Ok())
					break;
				res.type = mb.value.IsField ? mb.value.field->m_Type : mb.value.method->m_ReturnType;
				res.declaration = mb.value.IsField ? mb.value.field->m_DeclaringNode : mb.value.method->m_DeclaringNode;
				res.isFunction = !mb.value.IsField;
				break;
			}

			// Static member of the named type (own + inherited, most-derived first).
			for (const Scripting::ObjectType* t = obj; t; )
			{
				if (const auto* f = t->GetFieldByName(member)) { res.type = f->m_Type; res.declaration = f->m_DeclaringNode; break; }
				const Scripting::ObjectType::MethodInfo* m = t->FindMethodByName(member);
				if (m) { res.type = m->m_ReturnType; res.declaration = m->m_DeclaringNode; res.isFunction = true; break; }
				t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr;
			}
			break;
		}
		case Scripting::NodeType::StaticAccess: {
			const auto& sa = static_cast<const Scripting::StaticAccessNode&>(node);
			std::string prefix = StaticAccessPath(&sa);

			if (!prefix.empty())
			{
				const Scripting::SymbolInfo* info = QuerySym(m_Model, prefix, unitId);
				if (info)
				{
					res.declaration = info->declNode;
					res.isFunction = (info->kind == Scripting::SymbolKind::Function);
					res.type = info->type ? Scripting::TypeHandle::UnwrapToValue(info->type) : nullptr;
					if (res.isFunction && !res.type && !info->functions.Empty())
						res.type = info->functions[0]->type;
					break;
				}
				// Owner type + member the qualified walk could not close: resolve it on the type chain.
				size_t colon = prefix.rfind("::");
				if (colon != std::string::npos)
						if (const Scripting::SymbolInfo* owner = QuerySym(m_Model, prefix.substr(0, colon), unitId))
						if (owner->kind == Scripting::SymbolKind::Type && owner->type)
							if (const Scripting::ObjectType* obj = UnwrapObject(owner->type))
							{
								const std::string member = prefix.substr(colon + 2);
								for (const Scripting::ObjectType* t = obj; t; )
								{
							if (const auto* f = t->GetFieldByName(member)) { res.type = f->m_Type; res.declaration = f->m_DeclaringNode; break; }
							const Scripting::ObjectType::MethodInfo* m = t->FindMethodByName(member);
							if (m) { res.type = m->m_ReturnType; res.declaration = m->m_DeclaringNode; res.isFunction = true; break; }
									t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr;
								}
							}
			}
			break;
		}
		default:
			break;
		}

		m_RefCache[&node] = res;
		return res;
	}

	const Scripting::Type* SemanticResolver::ResolveQualType(const Scripting::QualTypeNode& qt,
		const Core::Vector<const Scripting::AstNode*>& path) const
	{
		auto cached = m_TypeCache.find(&qt);
		if (cached != m_TypeCache.end())
			return cached->second;

		uint32 unitId = UnitIdOf(qt);
		const Scripting::Scope* ctx = FindFunctionIndex(path) >= 0 ? ContextFor(path).scope : nullptr;
		auto r = m_Model.ResolveTypeInUnitContext(qt, unitId, ctx);
		const Scripting::Type* type = r.Ok() ? r.value : nullptr;
		m_TypeCache[&qt] = type;
		return type;
	}

	void SemanticResolver::CollectNamespacePaths(std::unordered_set<std::string>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const Scripting::Scope*, std::vector<std::string>&)> walk =
			[&](const Scripting::Scope* scope, std::vector<std::string>& chain) {
				for (const auto& child : scope->GetChildren())
				{
					const Scripting::Scope* s = child.get();
					if (s->GetKind() == Scripting::Scope::Kind::Namespace && !s->GetName().empty())
					{
						chain.push_back(s->GetName());
						for (size_t i = 1; i <= chain.size(); ++i) // every prefix is a namespace path
						{
							std::string p;
							for (size_t j = chain.size() - i; j < chain.size(); ++j)
								p += (p.empty() ? "" : "::") + chain[j];
							out.insert(p);
						}
						walk(s, chain);
						chain.pop_back();
					}
					else
						walk(s, chain); // file / class / function layers pass through unchanged
				}
			};
		std::vector<std::string> chain;
		walk(m_Model.Symbols()->GetRoot(), chain);
	}

	void SemanticResolver::CollectGlobalSymbols(std::vector<GlobalSymbol>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const Scripting::Scope*)> walk = [&](const Scripting::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			auto kind = scope->GetKind();
			if (kind != Scripting::Scope::Kind::File && kind != Scripting::Scope::Kind::Namespace)
				return; // Class scopes hold fields/methods - reachable only via member access, never as globals.
						// Function/block layers hold locals, never project symbols. Emitting Class-scope symbols here
						// leaked every class's members into EVERY completion list (the wrong-object bug).
			Core::Vector<std::pair<Core::String, Scripting::SymbolInfo*>> entries;
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
			{
				GlobalSymbol gs;
				gs.name = name;
				gs.kind = info->kind;
				gs.type = info->type;
				gs.fn = info->functions.Empty() ? nullptr : info->functions[0];
				out.push_back(std::move(gs));
			}
		};
		walk(m_Model.Symbols()->GetRoot());
	}

	void SemanticResolver::CollectObjectTypes(std::vector<ObjectDecl>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const Scripting::Scope*)> walk = [&](const Scripting::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			Core::Vector<std::pair<Core::String, Scripting::SymbolInfo*>> entries;
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
			{
				if (info->kind != Scripting::SymbolKind::Type || !info->type)
					continue;
				const auto* obj = dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(info->type));
				if (obj)
					out.push_back({ obj, info->declNode });
			}
		};
		walk(m_Model.Symbols()->GetRoot());
	}

	void SemanticResolver::CollectDeclarationsByName(const std::string& simpleName, std::vector<const Scripting::AstNode*>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const Scripting::Scope*)> walk = [&](const Scripting::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			Core::Vector<std::pair<Core::String, Scripting::SymbolInfo*>> entries;
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
				if (name == simpleName && info->declNode)
					out.push_back(info->declNode);
		};
		walk(m_Model.Symbols()->GetRoot());
	}
} // namespace C4LSP::Server
