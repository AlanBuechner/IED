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
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/LambdaNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/ForLoopNode.h"
#include <algorithm>

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

		/// Of a function name's overload set, the record whose declarations hold `fn`; else its first (overloads share
		/// their scope and containing type); null when `info` names no function.
		const Scripting::FunctionRecord* RecordAmong(const Scripting::SymbolInfo* info, const Scripting::AstNode& fn)
		{
			if (!info || info->kind != Scripting::SymbolKind::Function || info->functions.Empty())
				return nullptr;
			for (const Scripting::FunctionRecord* rec : info->functions)
				for (const Scripting::AstNode* declaration : rec->declarations)
					if (declaration == &fn)
						return rec;
			return info->functions[0];
		}

		bool IsTypeDeclaration(const Scripting::AstNode* node)
		{
			if (!node)
				return false;
			const Scripting::NodeType type = node->Type();
			return type == Scripting::NodeType::ObjectDefinition || type == Scripting::NodeType::EnumDefinition ||
				type == Scripting::NodeType::UnionDefinition || type == Scripting::NodeType::AliasDefinition;
		}

		/// What calling a function of type `callee` gives: a function type's return type; a resolved method already
		/// carries its return type.
		const Scripting::Type* ReturnTypeOf(const Scripting::Type* callee)
		{
			if (const auto* function = dynamic_cast<const Scripting::FunctionType*>(callee))
				return function->GetReturnType();
			return callee;
		}

		const Scripting::ObjectType* UnwrapObject(const Scripting::Type* t)
		{
			return dynamic_cast<const Scripting::ObjectType*>(Scripting::TypeHandle::UnwrapToValue(t));
		}

		/// What `->` reaches: the type a pointer points to (past aliases and references to it); `t` itself otherwise (BUG-353).
		const Scripting::Type* Pointee(const Scripting::Type* t)
		{
			const auto* pointer = dynamic_cast<const Scripting::PointerType*>(Scripting::TypeHandle::UnwrapToValue(t));
			return pointer ? pointer->GetPointeeType() : t;
		}

		/// A simple or qualified name as the compiler finds it: from `context` (the enclosing namespace or class) out to
		/// the root, a qualified name the context does not lead to from the root. The model's own QuerySymbol verb, so the
		/// LSP never resolves a name differently.
		const Scripting::SymbolInfo* FindVisibleQualified(const Scripting::ProgramModel& model, const std::string& name, uint32 unitId,
			const Scripting::Scope* context = nullptr)
		{
			auto r = model.QuerySymbol(name, unitId, context);
			return r.Ok() ? r.value : nullptr;
		}

		/// The node that declares what `info` names: its own, else its first record's first (a function's symbol keeps
		/// no node; its records have every one).
		const Scripting::AstNode* DeclarationOf(const Scripting::SymbolInfo& info)
		{
			if (info.declNode)
				return info.declNode;
			for (const Scripting::FunctionRecord* rec : info.functions)
				if (rec && !rec->declarations.Empty())
					return rec->declarations[0];
			return nullptr;
		}

		NodeResolution SymbolResolution(const Scripting::SymbolInfo& info)
		{
			NodeResolution res;
			res.declaration = DeclarationOf(info);
			res.isFunction = info.kind == Scripting::SymbolKind::Function;
			res.type = info.type ? Scripting::TypeHandle::UnwrapToValue(info.type) : nullptr;
			if (res.isFunction && !res.type && !info.functions.Empty())
				res.type = info.functions[0]->type;
			return res;
		}

		/// A member function template `object` or a base declares (`SortBy` of `entries.SortBy(...)`): it has no method
		/// until a call instantiates it (BUG-402).
		NodeResolution MemberTemplateOf(const Scripting::ProgramModel& model, const Scripting::ObjectType* object, const std::string& member)
		{
			NodeResolution res;
			const Core::Vector<const Scripting::FunctionDefinitionNode*> templates = model.QueryMemberTemplates(object, member);
			if (!templates.Empty())
			{
				res.declaration = templates[0];
				res.isFunction = true;
			}
			return res;
		}

		/// What `Owner::member` names when `owner` is a type: an enum's member (BUG-404), else a field, a method or a
		/// member template of the type or a base, the most derived first.
		NodeResolution MemberOfNamedType(const Scripting::ProgramModel& model, const Scripting::SymbolInfo& owner, const std::string& member)
		{
			NodeResolution res;
			if (owner.kind != Scripting::SymbolKind::Type || !owner.type)
				return res;
			const Scripting::Type* type = Scripting::TypeHandle::UnwrapToValue(owner.type);
			if (const auto* enumeration = dynamic_cast<const Scripting::EnumType*>(type))
			{
				if (const Scripting::EnumType::MemberInfo* m = enumeration->GetMemberByName(member))
					return { enumeration, m->m_DeclaringNode, false };
				return res;
			}
			const auto* object = dynamic_cast<const Scripting::ObjectType*>(type);
			for (const Scripting::ObjectType* t = object; t; t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr)
			{
				if (const auto* f = t->GetFieldByName(member))
					return { f->m_Type, f->m_DeclaringNode, false };
				if (const Scripting::ObjectType::MethodInfo* m = t->FindMethodByName(member))
					return { m->m_ReturnType, m->m_DeclaringNode, true };
			}
			return object ? MemberTemplateOf(model, object, member) : res;
		}

		/// What `value.member` names when `value` is an `object`: a field or a method (the model's member query), else a
		/// member template.
		NodeResolution MemberOfValue(const Scripting::ProgramModel& model, const Scripting::ObjectType* object, const std::string& member)
		{
			auto mb = model.QueryMember(static_cast<const Scripting::Type*>(object), member);
			if (!mb.Ok())
				return MemberTemplateOf(model, object, member);
			if (mb.value.IsField)
				return { mb.value.field->m_Type, mb.value.field->m_DeclaringNode, false };
			return { mb.value.method->m_ReturnType, mb.value.method->m_DeclaringNode, true };
		}

		/// The declaration of `name` that `node` itself makes, as a scope a reference inside it sees: a block's local, a
		/// for statement's variable, a lambda's init capture or parameter, a function's parameter (BUG-403). Null when it
		/// declares none.
		const Scripting::AstNode* DeclaredIn(const Scripting::AstNode& node, const std::string& name)
		{
			auto named = [&name](const Core::Vector<Ref<Scripting::ParameterNode>>& parameters) -> const Scripting::AstNode* {
				for (const auto& param : parameters)
					if (param && param->HasName() && param->GetNameToken().GetText() == name)
						return param.Get();
				return nullptr;
			};
			auto declares = [&name](const Scripting::AstNode* statement) {
				if (!statement || statement->Type() != Scripting::NodeType::VariableDefinition)
					return false;
				for (const auto& d : static_cast<const Scripting::VariableDefinitionNode&>(*statement).GetDeclarators())
					if (d.m_NameToken.GetText() == name)
						return true;
				return false;
			};
			switch (node.Type())
			{
			case Scripting::NodeType::CodeBlock:
				for (const auto& statement : static_cast<const Scripting::CodeBlockNode&>(node).GetStatements())
					if (declares(statement.Get()))
						return statement.Get();
				return nullptr;
			case Scripting::NodeType::ForLoop: {
				const auto& loop = static_cast<const Scripting::ForLoopNode&>(node);
				if (loop.IsRangeBased())
					return loop.GetRangeVarNameToken().GetText() == name ? &node : nullptr;
				return declares(loop.GetInitClause().Get()) ? loop.GetInitClause().Get() : nullptr;
			}
			case Scripting::NodeType::LambdaExpression: {
				const auto& lambda = static_cast<const Scripting::LambdaNode&>(node);
				for (const Scripting::LambdaCapture& capture : lambda.GetCaptures())
					if (capture.declaration && capture.name.GetText() == name)
						return capture.declaration.Get();
				return named(lambda.GetParameters());
			}
			case Scripting::NodeType::FunctionDefinition:
				return named(static_cast<const Scripting::FunctionDefinitionNode&>(node).GetParameters());
			default:
				return nullptr;
			}
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
			if (const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, candidate, UnitIdOf(objNode)))
				if (info->kind == Scripting::SymbolKind::Type && info->type)
					return UnwrapObject(info->type);
		}
		return nullptr;
	}

	// Outside any function (a field, a base type, a namespace's variable): its declarations' scope.
	SemanticResolver::Ctx SemanticResolver::ContextFor(const Core::Vector<const Scripting::AstNode*>& path) const
	{
		Ctx ctx;
		int fi = FindFunctionIndex(path);
		if (fi < 0)
		{
			ctx.unitId = path.Count() ? UnitIdOf(*path.Back()) : 0;
			ctx.enclosingType = EnclosingObject(path);
			ctx.scope = DeclarationScope(path);
			return ctx;
		}
		const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(*path[fi]);
		ctx.unitId = UnitIdOf(fn);
		if (!fn.GetName())
			return ctx;

		if (const Scripting::FunctionRecord* rec = RecordOf(path, fi, ctx.unitId))
		{
			ctx.enclosingType = rec->containingType ? rec->containingType : EnclosingObject(path);
			ctx.scope = rec->scope; // the function's enclosing namespace/class - unqualified lookup context
		}
		return ctx;
	}

	// Where a declaration outside any function is: its innermost type's scope, else its innermost namespace's; null at
	// file level. A field's type in `namespace Ied { class Server { LspPool* lsp; } }` is looked up from there, as the
	// compiler looks it up, not from the file's top.
	const Scripting::Scope* SemanticResolver::DeclarationScope(const Core::Vector<const Scripting::AstNode*>& path) const
	{
		if (const Scripting::ObjectType* object = EnclosingObject(path))
			return m_Model.TypeScope(object);
		std::string qualified;
		for (const Scripting::AstNode* node : path)
			if (node->Type() == Scripting::NodeType::Namespace && !static_cast<const Scripting::NamespaceNode&>(*node).IsRoot())
				qualified += (qualified.empty() ? "" : "::") + std::string(static_cast<const Scripting::NamespaceNode&>(*node).Name());
		if (qualified.empty())
			return nullptr;
		const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, qualified, path.Count() ? UnitIdOf(*path.Back()) : 0);
		return info && info->kind == Scripting::SymbolKind::Namespace ? info->scope : nullptr;
	}

	// The record HIR lowers for the function `path[fi]` declares or defines, so containingType and scope match the
	// compiler's. Its name is registered in its class's scope (a method defined in its class) or under every enclosing
	// namespace (`N::Find` for a `Find` in `namespace N`, tried with all of them first, then fewer); of the overloads a
	// name finds, the one whose declarations hold this node. An argument-free query is the last resort only: it misses
	// every function that takes parameters.
	const Scripting::FunctionRecord* SemanticResolver::RecordOf(const Core::Vector<const Scripting::AstNode*>& path, int fi, uint32 unitId) const
	{
		const auto& fn = static_cast<const Scripting::FunctionDefinitionNode&>(*path[fi]);
		const std::string name = JoinSegments(*fn.GetName());
		const Scripting::ObjectType* receiver = EnclosingObject(path);
		if (receiver)
			if (const Scripting::FunctionRecord* rec = RecordAmong(FindVisibleQualified(m_Model, name, 0, m_Model.TypeScope(receiver)), fn))
				return rec;

		std::vector<std::string> namespaces; // innermost first
		for (int i = fi - 1; i >= 0; --i)
			if (path[i]->Type() == Scripting::NodeType::Namespace)
				namespaces.push_back(static_cast<const Scripting::NamespaceNode&>(*path[i]).Name());
		for (size_t depth = namespaces.size() + 1; depth-- > 0;)
		{
			std::string qualified = name;
			for (size_t k = 0; k < depth; ++k)
				qualified = namespaces[k] + "::" + qualified;
			if (const Scripting::FunctionRecord* rec = RecordAmong(FindVisibleQualified(m_Model, qualified, unitId), fn))
				return rec;
		}

		// A class template's method: one record per instance, which no scope names by the method alone (BUG-408).
		if (const Scripting::FunctionRecord* rec = RecordDeclaring(fn))
			return rec;

		Scripting::LookupContext lookup;
		lookup.unitId = unitId;
		lookup.enclosingType = receiver;
		const Scripting::FunctionRecord* rec = nullptr;
		if (m_Model.QueryFunction(name, nullptr, nullptr, {}, lookup, &rec).Ok() || (receiver && m_Model.QueryFunction(name, receiver, nullptr, {}, lookup, &rec).Ok()))
			return rec;
		return nullptr;
	}

	const Scripting::FunctionRecord* SemanticResolver::RecordDeclaring(const Scripting::AstNode& fn) const
	{
		if (!m_RecordsIndexed)
		{
			Core::Vector<const Scripting::FunctionRecord*> records;
			m_Model.AllFunctionRecords(records);
			for (const Scripting::FunctionRecord* record : records)
				for (const Scripting::AstNode* declaration : record->declarations)
					m_RecordByDeclaration.emplace(declaration, record); // the first record a declaration has
			m_RecordsIndexed = true;
		}
		auto found = m_RecordByDeclaration.find(&fn);
		return found == m_RecordByDeclaration.end() ? nullptr : found->second;
	}

	const Scripting::SymbolInfo* SemanticResolver::Lookup(const std::string& name, uint32 unitId,
		const Core::Vector<const Scripting::AstNode*>* path) const
	{
		if (!m_Model.IsValid())
			return nullptr;
		return FindVisibleQualified(m_Model, name, unitId, path ? ContextFor(*path).scope : nullptr);
	}

	// Innermost scope first, out to the function: a block's locals, a for statement's variable, a lambda's parameters,
	// then the function's parameters. Stopping at the function keeps a same-named local of an outer (sibling) function
	// out - the rule HIR's scope chain enforces.
	bool SemanticResolver::ResolveLocal(const std::string& name, const Core::Vector<const Scripting::AstNode*>& path,
		const Scripting::AstNode*& declOut, const Scripting::Type*& typeOut) const
	{
		declOut = nullptr;
		typeOut = nullptr;
		const int fi = FindFunctionIndex(path);
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0 && i >= fi; --i)
			if (const Scripting::AstNode* declaration = DeclaredIn(*path[i], name))
			{
				declOut = declaration;
				typeOut = DeclaredType(*declaration, path);
				return true;
			}
		return false;
	}

	// The type a parameter, a local or a range-for variable is declared with; a variable declared without one (an init
	// capture, `n` of `[n = count]`) has its initializer's.
	const Scripting::Type* SemanticResolver::DeclaredType(const Scripting::AstNode& declaration, const Core::Vector<const Scripting::AstNode*>& path) const
	{
		const Scripting::AstNode* written = nullptr;
		switch (declaration.Type())
		{
		case Scripting::NodeType::Parameter: written = static_cast<const Scripting::ParameterNode&>(declaration).GetType().Get(); break;
		case Scripting::NodeType::VariableDefinition: {
			const auto& variable = static_cast<const Scripting::VariableDefinitionNode&>(declaration);
			if (!variable.GetType() && variable.GetInitializer())
				return ResolveReference(*variable.GetInitializer(), path).type;
			written = variable.GetType().Get();
			break;
		}
		case Scripting::NodeType::ForLoop: written = static_cast<const Scripting::ForLoopNode&>(declaration).GetInitClause().Get(); break;
		default: break;
		}
		if (!written || written->Type() != Scripting::NodeType::QualType)
			return nullptr;
		return ResolveQualType(static_cast<const Scripting::QualTypeNode&>(*written), path);
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

		// 3) A function name in expression position (call callee, decay, or bare reference). The arguments are not
		//    known here, so any overload names it: the one an argument-free call picks, else the first of the set.
		const Scripting::FunctionRecord* picked = nullptr;
		auto fn = m_Model.QueryFunction(name, nullptr, nullptr, Core::Vector<const Scripting::Type*>(), qctx, &picked);
		const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, name, 0, ctx.scope);
		const bool overloads = info && info->kind == Scripting::SymbolKind::Function && !info->functions.Empty();
		if (fn.Ok() || overloads)
		{
			const Scripting::FunctionRecord* rec = fn.Ok() ? picked : info->functions[0];
			NodeResolution res;
			res.isFunction = true;
			res.type = fn.Ok() ? fn.value : rec->type;
			res.declaration = info ? info->declNode : nullptr;
			if (!res.declaration && rec && !rec->declarations.Empty())
				res.declaration = rec->declarations[0]; // a function's symbol keeps no node; its record has every one
			return res;
		}

		// 4) A type named as a value: the callee of a constructor call (`StringView(text)`).
		if (info && info->kind == Scripting::SymbolKind::Type && info->type)
			return { Scripting::TypeHandle::UnwrapToValue(info->type), info->declNode, false };

		// 5) An implicit-this method a base class declares (the class scope holds only the type's own).
		for (const Scripting::ObjectType* t = ctx.enclosingType; t; t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr)
			if (const Scripting::ObjectType::MethodInfo* method = t->FindMethodByName(name))
				return { method->m_ReturnType, method->m_DeclaringNode, true };
		return {};
	}

	// The type a member access's base names (`Counter` of `Counter.Make`), looked up from where it is written as the
	// compiler does; null when the base is a value - a local or a parameter of that name comes first.
	const Scripting::SymbolInfo* SemanticResolver::TypeNamedBy(const Scripting::AstNode& base, const Core::Vector<const Scripting::AstNode*>& path) const
	{
		std::string written;
		if (base.Type() == Scripting::NodeType::IdentifierExpression)
			written = static_cast<const Scripting::IdentifierExpressionNode&>(base).GetName();
		else if (base.Type() == Scripting::NodeType::StaticAccess || base.Type() == Scripting::NodeType::NamespaceQualifiedName)
			written = StaticAccessPath(&base);
		const Scripting::AstNode* local = nullptr;
		const Scripting::Type* localType = nullptr;
		if (written.empty() || ResolveLocal(written, path, local, localType))
			return nullptr;
		const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, written, UnitIdOf(base), ContextFor(path).scope);
		return info && info->kind == Scripting::SymbolKind::Type && info->type ? info : nullptr;
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

			// `Type.member` / `Ns::Type.member`: the base names a TYPE (static member), checked first - exactly HIR's
			// ResolveMemberAccess ordering.
			if (const Scripting::SymbolInfo* owner = TypeNamedBy(*base, path))
			{
				res = MemberOfNamedType(m_Model, *owner, member);
				break;
			}
			// The base is an expression: resolve its value type, then the member on it.
			const NodeResolution baseRes = ResolveReference(*base, path);
			const Scripting::ObjectType* baseObj = baseRes.type ? UnwrapObject(ma.IsArrowAccess() ? Pointee(baseRes.type) : baseRes.type) : nullptr;
			if (baseObj)
				res = MemberOfValue(m_Model, baseObj, member);
			break;
		}
		case Scripting::NodeType::StaticAccess: {
			const std::string prefix = StaticAccessPath(&node);
			if (prefix.empty())
				break;
			const Scripting::Scope* context = ContextFor(path).scope;
			if (const Scripting::SymbolInfo* info = FindVisibleQualified(m_Model, prefix, unitId, context))
			{
				res = SymbolResolution(*info);
				break;
			}
			// Owner type + member the qualified walk could not close: an enum's member, or one on the type chain.
			const size_t colon = prefix.rfind("::");
			if (colon != std::string::npos)
				if (const Scripting::SymbolInfo* owner = FindVisibleQualified(m_Model, prefix.substr(0, colon), unitId, context))
					res = MemberOfNamedType(m_Model, *owner, prefix.substr(colon + 2));
			break;
		}
		case Scripting::NodeType::FunctionCall: {
			// A call's value - what `View()` gives `View().CodePointAt(i)`: what the callee returns, or the type a
			// constructor call names.
			const auto& call = static_cast<const Scripting::FunctionCallNode&>(node);
			if (!call.GetCallee())
				break;
			const NodeResolution callee = ResolveReference(*call.GetCallee(), path);
			if (callee.isFunction)
				res.type = ReturnTypeOf(callee.type);
			else if (IsTypeDeclaration(callee.declaration))
				res.type = callee.type;
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
		const Scripting::Scope* ctx = ContextFor(path).scope;
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

	void SemanticResolver::CollectFunctionDeclarations(const Scripting::AstNode* declaration, std::vector<const Scripting::AstNode*>& out) const
	{
		if (m_Model.IsValid() && declaration && declaration->Type() == Scripting::NodeType::FunctionDefinition)
		{
			Core::Vector<const Scripting::FunctionRecord*> records;
			m_Model.AllFunctionRecords(records);
			for (const Scripting::FunctionRecord* record : records)
			{
				if (std::find(record->declarations.begin(), record->declarations.end(), declaration) == record->declarations.end())
					continue;
				for (const Scripting::AstNode* node : record->declarations)
					if (std::find(out.begin(), out.end(), node) == out.end())
						out.push_back(node);
			}
		}
		if (std::find(out.begin(), out.end(), declaration) == out.end())
			out.push_back(declaration);
	}
} // namespace C4LSP::Server
