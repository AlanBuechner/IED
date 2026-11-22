#include "Server/SemanticResolver.h"
#include "Model/TypeHandle.h" // TypeHandle::UnwrapToValue (alias/reference -> value type)
#include "Features/TypeText.h" // DeclarationOfNamedType
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Model/AnnotationOverloads.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/ClassHierarchy.h"
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
		int FindFunctionIndex(const Core::Vector<const C4Compiler::AstNode*>& path)
		{
			for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
				if (path[i]->Type() == C4Compiler::NodeType::FunctionDefinition)
					return i;
			return -1;
		}

		uint32 UnitIdOf(const C4Compiler::AstNode& node) { return node.startLocation.sourceId; }

		/// Of a function name's overload set, the record whose declarations hold `fn`; else its first (overloads share
		/// their scope and containing type); null when `info` names no function.
		const C4Compiler::FunctionRecord* RecordAmong(const C4Compiler::SymbolInfo* info, const C4Compiler::AstNode& fn)
		{
			if (!info || info->kind != C4Compiler::SymbolKind::Function || info->functions.Empty())
				return nullptr;
			for (const C4Compiler::FunctionRecord* rec : info->functions)
				for (const C4Compiler::AstNode* declaration : rec->declarations)
					if (declaration == &fn)
						return rec;
			return info->functions[0];
		}

		bool IsTypeDeclaration(const C4Compiler::AstNode* node)
		{
			if (!node)
				return false;
			const C4Compiler::NodeType type = node->Type();
			return type == C4Compiler::NodeType::ObjectDefinition || type == C4Compiler::NodeType::EnumDefinition ||
				type == C4Compiler::NodeType::UnionDefinition || type == C4Compiler::NodeType::AliasDefinition;
		}

		/// What calling a function of type `callee` gives: a function type's return type; a resolved method already
		/// carries its return type.
		const C4Compiler::Type* ReturnTypeOf(const C4Compiler::Type* callee)
		{
			if (const auto* function = dynamic_cast<const C4Compiler::FunctionType*>(callee))
				return function->GetReturnType();
			return callee;
		}

		const C4Compiler::ObjectType* UnwrapObject(const C4Compiler::Type* t)
		{
			return dynamic_cast<const C4Compiler::ObjectType*>(C4Compiler::TypeHandle::UnwrapToValue(t));
		}

		/// What `->` reaches: the type a pointer points to (past aliases and references to it, BUG-353), an object's through
		/// its [operator("->")]; `t` itself otherwise.
		const C4Compiler::Type* Pointee(const C4Compiler::Type* t)
		{
			t = C4Compiler::ClassHierarchy::ThroughArrowOperators(t);
			const auto* pointer = dynamic_cast<const C4Compiler::PointerType*>(C4Compiler::TypeHandle::UnwrapToValue(t));
			return pointer ? pointer->GetPointeeType() : t;
		}

		/// A simple or qualified name as the compiler finds it: from `context` (the enclosing namespace or class) out to
		/// the root, a qualified name the context does not lead to from the root. The model's own QuerySymbol verb, so the
		/// LSP never resolves a name differently.
		const C4Compiler::SymbolInfo* FindVisibleQualified(const C4Compiler::ProgramModel& model, const std::string& name, uint32 unitId,
			const C4Compiler::Scope* context = nullptr)
		{
			auto r = model.QuerySymbol(name, unitId, context);
			return r.Ok() ? r.value : nullptr;
		}

		/// The node that declares what `info` names: its own, else its first record's first (a function's symbol keeps
		/// no node; its records have every one).
		const C4Compiler::AstNode* DeclarationOf(const C4Compiler::SymbolInfo& info)
		{
			if (info.declNode)
				return info.declNode;
			for (const C4Compiler::FunctionRecord* rec : info.functions)
				if (rec && !rec->declarations.Empty())
					return rec->declarations[0];
			return nullptr;
		}

		NodeResolution SymbolResolution(const C4Compiler::SymbolInfo& info)
		{
			NodeResolution res;
			res.declaration = DeclarationOf(info);
			res.isFunction = info.kind == C4Compiler::SymbolKind::Function;
			res.type = info.type ? C4Compiler::TypeHandle::UnwrapToValue(info.type) : nullptr;
			if (res.isFunction && !res.type && !info.functions.Empty())
				res.type = info.functions[0]->type;
			return res;
		}

		/// A member function template `object` or a base declares (`SortBy` of `entries.SortBy(...)`): it has no method
		/// until a call instantiates it (BUG-402).
		NodeResolution MemberTemplateOf(const C4Compiler::ProgramModel& model, const C4Compiler::ObjectType* object, const std::string& member)
		{
			NodeResolution res;
			const Core::Vector<const C4Compiler::FunctionDefinitionNode*> templates = model.QueryMemberTemplates(object, member);
			if (!templates.Empty())
			{
				res.declaration = templates[0];
				res.isFunction = true;
			}
			return res;
		}

		/// What `Owner::member` names when `owner` is a type: an enum's member (BUG-404), else a field, a method or a
		/// member template of the type or a base, the most derived first.
		NodeResolution MemberOfNamedType(const C4Compiler::ProgramModel& model, const C4Compiler::SymbolInfo& owner, const std::string& member)
		{
			NodeResolution res;
			if (owner.kind != C4Compiler::SymbolKind::Type || !owner.type)
				return res;
			const C4Compiler::Type* type = C4Compiler::TypeHandle::UnwrapToValue(owner.type);
			if (const auto* enumeration = dynamic_cast<const C4Compiler::EnumType*>(type))
			{
				if (const C4Compiler::EnumType::MemberInfo* m = enumeration->GetMemberByName(member))
					return { enumeration, m->m_DeclaringNode, false };
				return res;
			}
			const auto* object = dynamic_cast<const C4Compiler::ObjectType*>(type);
			for (const C4Compiler::ObjectType* t = object; t; t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr)
			{
				if (const auto* f = t->GetFieldByName(member))
					return { f->m_Type, f->m_DeclaringNode, false };
				if (const C4Compiler::ObjectType::MethodInfo* m = t->FindMethodByName(member))
					return { m->m_ReturnType, m->m_DeclaringNode, true };
			}
			return object ? MemberTemplateOf(model, object, member) : res;
		}

		/// What `value.member` names when `value` is an `object`: a field or a method (the model's member query), else a
		/// member template.
		NodeResolution MemberOfValue(const C4Compiler::ProgramModel& model, const C4Compiler::ObjectType* object, const std::string& member)
		{
			auto mb = model.QueryMember(static_cast<const C4Compiler::Type*>(object), member);
			if (!mb.Ok())
				return MemberTemplateOf(model, object, member);
			if (mb.value.IsField)
				return { mb.value.field->m_Type, mb.value.field->m_DeclaringNode, false };
			return { mb.value.method->m_ReturnType, mb.value.method->m_DeclaringNode, true };
		}

		/// The declaration of `name` that `node` itself makes, as a scope a reference inside it sees: a block's local, a
		/// for statement's variable, a lambda's init capture or parameter, a function's parameter (BUG-403). Null when it
		/// declares none.
		const C4Compiler::AstNode* DeclaredIn(const C4Compiler::AstNode& node, const std::string& name)
		{
			auto named = [&name](const Core::Vector<Ref<C4Compiler::ParameterNode>>& parameters) -> const C4Compiler::AstNode* {
				for (const auto& param : parameters)
					if (param && param->HasName() && param->GetNameToken().GetText() == name)
						return param.Get();
				return nullptr;
			};
			auto declares = [&name](const C4Compiler::AstNode* statement) {
				if (!statement || statement->Type() != C4Compiler::NodeType::VariableDefinition)
					return false;
				for (const auto& d : static_cast<const C4Compiler::VariableDefinitionNode&>(*statement).GetDeclarators())
					if (d.m_NameToken.GetText() == name)
						return true;
				return false;
			};
			switch (node.Type())
			{
			case C4Compiler::NodeType::CodeBlock:
				for (const auto& statement : static_cast<const C4Compiler::CodeBlockNode&>(node).GetStatements())
					if (declares(statement.Get()))
						return statement.Get();
				return nullptr;
			case C4Compiler::NodeType::ForLoop: {
				const auto& loop = static_cast<const C4Compiler::ForLoopNode&>(node);
				if (loop.IsRangeBased())
					return loop.GetRangeVarNameToken().GetText() == name ? &node : nullptr;
				return declares(loop.GetInitClause().Get()) ? loop.GetInitClause().Get() : nullptr;
			}
			case C4Compiler::NodeType::LambdaExpression: {
				const auto& lambda = static_cast<const C4Compiler::LambdaNode&>(node);
				for (const C4Compiler::LambdaCapture& capture : lambda.GetCaptures())
					if (capture.declaration && capture.name.GetText() == name)
						return capture.declaration.Get();
				return named(lambda.GetParameters());
			}
			case C4Compiler::NodeType::FunctionDefinition:
				return named(static_cast<const C4Compiler::FunctionDefinitionNode&>(node).GetParameters());
			default:
				return nullptr;
			}
		}

		std::string JoinSegments(const C4Compiler::NamespaceQualifiedNameNode& nqn)
		{
			std::string out;
			for (uint32 i = 0; i < nqn.SegmentCount(); ++i)
				out += (i ? "::" : "") + std::string(nqn.GetSegments()[i].nameToken.GetText());
			return out;
		}

		/// The qualified path of a StaticAccess chain (`A::B::C`) outermost-first, or "" if unjoinable.
		std::string StaticAccessPath(const C4Compiler::AstNode* node)
		{
			std::vector<std::string> segments; // innermost first
			const C4Compiler::AstNode* cur = node;
			while (cur && cur->Type() == C4Compiler::NodeType::StaticAccess)
			{
				const auto& inner = static_cast<const C4Compiler::StaticAccessNode&>(*cur);
				segments.push_back(inner.GetMemberNameToken().GetText());
				cur = inner.GetBaseType().Get();
			}
			std::string prefix;
			if (cur && cur->Type() == C4Compiler::NodeType::IdentifierExpression)
			{
				segments.push_back(static_cast<const C4Compiler::IdentifierExpressionNode&>(*cur).GetName());
				std::reverse(segments.begin(), segments.end()); // outermost first
				for (size_t i = 0; i < segments.size(); ++i)
					prefix += (i ? "::" : "") + segments[i];
			}
			else if (const auto* nqn = dynamic_cast<const C4Compiler::NamespaceQualifiedNameNode*>(cur))
			{
				for (uint32 i = 0; i < nqn->SegmentCount(); ++i)
					prefix += (prefix.empty() ? "" : "::") + std::string(nqn->GetSegments()[i].nameToken.GetText());
				for (const auto& s : segments)
					prefix += "::" + s;
			}
			return prefix;
		}
	}

	const C4Compiler::ObjectType* SemanticResolver::EnclosingObject(const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		int oi = -1;
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
			if (path[i]->Type() == C4Compiler::NodeType::ObjectDefinition)
			{
				oi = i;
				break;
			}
		if (oi < 0)
			return nullptr;

		const auto& objNode = static_cast<const C4Compiler::ObjectDefinitionNode&>(*path[oi]);
		std::string qualified = objNode.GetNameToken().GetText();
		std::vector<std::string> namespaces; // innermost first, for the prefix join below
		for (int i = oi - 1; i >= 0; --i)
			if (path[i]->Type() == C4Compiler::NodeType::Namespace)
				namespaces.push_back(static_cast<const C4Compiler::NamespaceNode&>(*path[i]).Name());

		for (size_t depth = 0; depth <= namespaces.size(); ++depth)
		{
			std::string candidate = qualified;
			for (size_t k = 0; k < depth; ++k) // innermost-first, then reverse to outermost-first
				candidate = namespaces[k] + "::" + candidate;
			if (const C4Compiler::SymbolInfo* info = FindVisibleQualified(m_Model, candidate, UnitIdOf(objNode)))
				if (info->kind == C4Compiler::SymbolKind::Type && info->type)
					return UnwrapObject(info->type);
		}
		return nullptr;
	}

	// Outside any function (a field, a base type, a namespace's variable): its declarations' scope.
	SemanticResolver::Ctx SemanticResolver::ContextFor(const Core::Vector<const C4Compiler::AstNode*>& path) const
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
		const auto& fn = static_cast<const C4Compiler::FunctionDefinitionNode&>(*path[fi]);
		ctx.unitId = UnitIdOf(fn);
		if (!fn.GetName())
			return ctx;

		if (const C4Compiler::FunctionRecord* rec = RecordOf(path, fi, ctx.unitId))
		{
			ctx.enclosingType = rec->containingType ? rec->containingType : EnclosingObject(path);
			ctx.scope = rec->scope; // the function's enclosing namespace/class - unqualified lookup context
		}
		return ctx;
	}

	// Where a declaration outside any function is: its innermost type's scope, else its innermost namespace's; null at
	// file level. A field's type in `namespace Ied { class Server { LspPool* lsp; } }` is looked up from there, as the
	// compiler looks it up, not from the file's top.
	const C4Compiler::Scope* SemanticResolver::DeclarationScope(const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		if (const C4Compiler::ObjectType* object = EnclosingObject(path))
			return m_Model.TypeScope(object);
		std::string qualified;
		for (const C4Compiler::AstNode* node : path)
			if (node->Type() == C4Compiler::NodeType::Namespace && !static_cast<const C4Compiler::NamespaceNode&>(*node).IsRoot())
				qualified += (qualified.empty() ? "" : "::") + std::string(static_cast<const C4Compiler::NamespaceNode&>(*node).Name());
		if (qualified.empty())
			return nullptr;
		const C4Compiler::SymbolInfo* info = FindVisibleQualified(m_Model, qualified, path.Count() ? UnitIdOf(*path.Back()) : 0);
		return info && info->kind == C4Compiler::SymbolKind::Namespace ? info->scope : nullptr;
	}

	// The record HIR lowers for the function `path[fi]` declares or defines, so containingType and scope match the
	// compiler's. Its name is registered in its class's scope (a method defined in its class) or under every enclosing
	// namespace (`N::Find` for a `Find` in `namespace N`, tried with all of them first, then fewer); of the overloads a
	// name finds, the one whose declarations hold this node. An argument-free query is the last resort only: it misses
	// every function that takes parameters.
	const C4Compiler::FunctionRecord* SemanticResolver::RecordOf(const Core::Vector<const C4Compiler::AstNode*>& path, int fi, uint32 unitId) const
	{
		const auto& fn = static_cast<const C4Compiler::FunctionDefinitionNode&>(*path[fi]);
		const std::string name = JoinSegments(*fn.GetName());
		const C4Compiler::ObjectType* receiver = EnclosingObject(path);
		if (receiver)
			if (const C4Compiler::FunctionRecord* rec = RecordAmong(FindVisibleQualified(m_Model, name, 0, m_Model.TypeScope(receiver)), fn))
				return rec;

		std::vector<std::string> namespaces; // innermost first
		for (int i = fi - 1; i >= 0; --i)
			if (path[i]->Type() == C4Compiler::NodeType::Namespace)
				namespaces.push_back(static_cast<const C4Compiler::NamespaceNode&>(*path[i]).Name());
		for (size_t depth = namespaces.size() + 1; depth-- > 0;)
		{
			std::string qualified = name;
			for (size_t k = 0; k < depth; ++k)
				qualified = namespaces[k] + "::" + qualified;
			if (const C4Compiler::FunctionRecord* rec = RecordAmong(FindVisibleQualified(m_Model, qualified, unitId), fn))
				return rec;
		}

		// A class template's method: one record per instance, which no scope names by the method alone (BUG-408).
		if (const C4Compiler::FunctionRecord* rec = RecordDeclaring(fn))
			return rec;

		C4Compiler::LookupContext lookup;
		lookup.unitId = unitId;
		lookup.enclosingType = receiver;
		const C4Compiler::FunctionRecord* rec = nullptr;
		if (m_Model.QueryFunction(name, nullptr, nullptr, {}, lookup, &rec).Ok() || (receiver && m_Model.QueryFunction(name, receiver, nullptr, {}, lookup, &rec).Ok()))
			return rec;
		return nullptr;
	}

	const C4Compiler::FunctionRecord* SemanticResolver::RecordDeclaring(const C4Compiler::AstNode& fn) const
	{
		if (!m_RecordsIndexed)
		{
			Core::Vector<const C4Compiler::FunctionRecord*> records;
			m_Model.AllFunctionRecords(records);
			for (const C4Compiler::FunctionRecord* record : records)
				for (const C4Compiler::AstNode* declaration : record->declarations)
					m_RecordByDeclaration.emplace(declaration, record); // the first record a declaration has
			m_RecordsIndexed = true;
		}
		auto found = m_RecordByDeclaration.find(&fn);
		return found == m_RecordByDeclaration.end() ? nullptr : found->second;
	}

	const C4Compiler::SymbolInfo* SemanticResolver::Lookup(const std::string& name, uint32 unitId,
		const Core::Vector<const C4Compiler::AstNode*>* path) const
	{
		if (!m_Model.IsValid())
			return nullptr;
		return FindVisibleQualified(m_Model, name, unitId, path ? ContextFor(*path).scope : nullptr);
	}

	// Innermost scope first, out to the function: a block's locals, a for statement's variable, a lambda's parameters,
	// then the function's parameters. Stopping at the function keeps a same-named local of an outer (sibling) function
	// out - the rule HIR's scope chain enforces.
	bool SemanticResolver::ResolveLocal(const std::string& name, const Core::Vector<const C4Compiler::AstNode*>& path,
		const C4Compiler::AstNode*& declOut, const C4Compiler::Type*& typeOut) const
	{
		declOut = nullptr;
		typeOut = nullptr;
		const int fi = FindFunctionIndex(path);
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0 && i >= fi; --i)
			if (const C4Compiler::AstNode* declaration = DeclaredIn(*path[i], name))
			{
				declOut = declaration;
				typeOut = DeclaredType(*declaration, path);
				return true;
			}
		return false;
	}

	// The type a parameter, a local or a range-for variable is declared with; a variable declared without one (an init
	// capture, `n` of `[n = count]`) has its initializer's.
	const C4Compiler::Type* SemanticResolver::DeclaredType(const C4Compiler::AstNode& declaration, const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		const C4Compiler::AstNode* written = nullptr;
		switch (declaration.Type())
		{
		case C4Compiler::NodeType::Parameter: written = static_cast<const C4Compiler::ParameterNode&>(declaration).GetType().Get(); break;
		case C4Compiler::NodeType::VariableDefinition: {
			const auto& variable = static_cast<const C4Compiler::VariableDefinitionNode&>(declaration);
			if (!variable.GetType() && variable.GetInitializer())
				return ResolveReference(*variable.GetInitializer(), path).type;
			written = variable.GetType().Get();
			break;
		}
		case C4Compiler::NodeType::ForLoop: written = static_cast<const C4Compiler::ForLoopNode&>(declaration).GetInitClause().Get(); break;
		default: break;
		}
		if (!written || written->Type() != C4Compiler::NodeType::QualType)
			return nullptr;
		return ResolveQualType(static_cast<const C4Compiler::QualTypeNode&>(*written), path);
	}

	NodeResolution SemanticResolver::ResolveIdentifier(const std::string& name, const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		if (name.empty() || name == "this")
			return {};

		// 1) Lexical locals + parameters (HIR's FindLocal equivalent, derived from the AST path).
		const C4Compiler::AstNode* localDecl = nullptr;
		const C4Compiler::Type* localType = nullptr;
		if (ResolveLocal(name, path, localDecl, localType))
			return { localType, localDecl, false };

		// 2) The model's own variable query: enclosing-type field (implicit this), then global. Build-time
		//    visibility (unitId 0) so a name defined in any file is found - the same rule the compiler uses
		//    when it builds the program-wide model, and what an editor needs for completion/navigation.
		Ctx ctx = ContextFor(path);
		C4Compiler::LookupContext qctx;
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
				for (const C4Compiler::ObjectType* t = ctx.enclosingType; t; )
				{
					if (const auto* f = t->GetFieldByName(name)) { res.declaration = f->m_DeclaringNode; break; }
					t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr;
				}
			}
			return res;
		}

		// 3) A function name in expression position (call callee, decay, or bare reference). The arguments are not
		//    known here, so any overload names it: the one an argument-free call picks, else the first of the set.
		const C4Compiler::FunctionRecord* picked = nullptr;
		auto fn = m_Model.QueryFunction(name, nullptr, nullptr, Core::Vector<const C4Compiler::Type*>(), qctx, &picked);
		const C4Compiler::SymbolInfo* info = FindVisibleQualified(m_Model, name, 0, ctx.scope);
		const bool overloads = info && info->kind == C4Compiler::SymbolKind::Function && !info->functions.Empty();
		if (fn.Ok() || overloads)
		{
			const C4Compiler::FunctionRecord* rec = fn.Ok() ? picked : info->functions[0];
			NodeResolution res;
			res.isFunction = true;
			res.type = fn.Ok() ? fn.value : rec->type;
			res.declaration = info ? info->declNode : nullptr;
			if (!res.declaration && rec && !rec->declarations.Empty())
				res.declaration = rec->declarations[0]; // a function's symbol keeps no node; its record has every one
			return res;
		}

		// 4) A type named as a value: the callee of a constructor call (`StringView(text)`).
		if (info && info->kind == C4Compiler::SymbolKind::Type && info->type)
			return { C4Compiler::TypeHandle::UnwrapToValue(info->type), info->declNode, false };

		// 5) An implicit-this method a base class declares (the class scope holds only the type's own).
		for (const C4Compiler::ObjectType* t = ctx.enclosingType; t; t = t->GetBaseTypes().Count() ? t->GetBaseTypes()[0] : nullptr)
			if (const C4Compiler::ObjectType::MethodInfo* method = t->FindMethodByName(name))
				return { method->m_ReturnType, method->m_DeclaringNode, true };
		return {};
	}

	// The type a member access's base names (`Counter` of `Counter.Make`), looked up from where it is written as the
	// compiler does; null when the base is a value - a local or a parameter of that name comes first.
	const C4Compiler::SymbolInfo* SemanticResolver::TypeNamedBy(const C4Compiler::AstNode& base, const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		std::string written;
		if (base.Type() == C4Compiler::NodeType::IdentifierExpression)
			written = static_cast<const C4Compiler::IdentifierExpressionNode&>(base).GetName();
		else if (base.Type() == C4Compiler::NodeType::StaticAccess || base.Type() == C4Compiler::NodeType::NamespaceQualifiedName)
			written = StaticAccessPath(&base);
		const C4Compiler::AstNode* local = nullptr;
		const C4Compiler::Type* localType = nullptr;
		if (written.empty() || ResolveLocal(written, path, local, localType))
			return nullptr;
		const C4Compiler::SymbolInfo* info = FindVisibleQualified(m_Model, written, UnitIdOf(base), ContextFor(path).scope);
		return info && info->kind == C4Compiler::SymbolKind::Type && info->type ? info : nullptr;
	}

	NodeResolution SemanticResolver::ResolveReference(const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		auto cached = m_RefCache.find(&node);
		if (cached != m_RefCache.end())
			return cached->second;

		NodeResolution res;
		uint32 unitId = UnitIdOf(node);

		switch (node.Type())
		{
		case C4Compiler::NodeType::IdentifierExpression: {
			res = ResolveIdentifier(static_cast<const C4Compiler::IdentifierExpressionNode&>(node).GetName(), path);
			break;
		}
		case C4Compiler::NodeType::MemberAccess: {
			const auto& ma = static_cast<const C4Compiler::MemberAccessNode&>(node);
			if (!ma.GetBaseObject())
				break;
			const std::string member = ma.GetMemberNameToken().GetText();
			const C4Compiler::AstNode* base = ma.GetBaseObject().Get();

			// `Type.member` / `Ns::Type.member`: the base names a TYPE (static member), checked first - exactly HIR's
			// ResolveMemberAccess ordering.
			if (const C4Compiler::SymbolInfo* owner = TypeNamedBy(*base, path))
			{
				res = MemberOfNamedType(m_Model, *owner, member);
				break;
			}
			// The base is an expression: resolve its value type, then the member on it.
			const NodeResolution baseRes = ResolveReference(*base, path);
			const C4Compiler::ObjectType* baseObj = baseRes.type ? UnwrapObject(ma.IsArrowAccess() ? Pointee(baseRes.type) : baseRes.type) : nullptr;
			if (baseObj)
				res = MemberOfValue(m_Model, baseObj, member);
			break;
		}
		case C4Compiler::NodeType::StaticAccess: {
			const std::string prefix = StaticAccessPath(&node);
			if (prefix.empty())
				break;
			const C4Compiler::Scope* context = ContextFor(path).scope;
			if (const C4Compiler::SymbolInfo* info = FindVisibleQualified(m_Model, prefix, unitId, context))
			{
				res = SymbolResolution(*info);
				break;
			}
			// Owner type + member the qualified walk could not close: an enum's member, or one on the type chain.
			const size_t colon = prefix.rfind("::");
			if (colon != std::string::npos)
				if (const C4Compiler::SymbolInfo* owner = FindVisibleQualified(m_Model, prefix.substr(0, colon), unitId, context))
					res = MemberOfNamedType(m_Model, *owner, prefix.substr(colon + 2));
			break;
		}
		case C4Compiler::NodeType::FunctionCall: {
			// A call's value - what `View()` gives `View().CodePointAt(i)`: what the callee returns, or the type a
			// constructor call names.
			const auto& call = static_cast<const C4Compiler::FunctionCallNode&>(node);
			if (!call.GetCallee())
				break;
			const NodeResolution callee = ResolveReference(*call.GetCallee(), path);
			if (callee.isFunction)
				res.type = ReturnTypeOf(callee.type);
			else if (IsTypeDeclaration(callee.declaration))
				res.type = callee.type;
			break;
		}
		case C4Compiler::NodeType::NamespaceQualifiedName: {
			// Written as a value, the name is a type's (`Box<int32>{1}`): resolved as a type node made of it would be.
			Ref<C4Compiler::NamespaceQualifiedNameNode> name(const_cast<C4Compiler::NamespaceQualifiedNameNode*>(static_cast<const C4Compiler::NamespaceQualifiedNameNode*>(&node)));
			const Ref<C4Compiler::QualTypeNode> written = C4Compiler::QualTypeNode::FromParts(name, nullptr, {}, false);
			auto type = m_Model.ResolveTypeInUnitContext(*written, unitId, ContextFor(path).scope);
			if (type.Ok())
			{
				res.type = type.value;
				res.declaration = Features::DeclarationOfNamedType(type.value);
			}
			break;
		}
		default:
			break;
		}

		m_RefCache[&node] = res;
		return res;
	}

	const C4Compiler::DeclaredAnnotations& SemanticResolver::Annotations() const { return m_Model.Annotations(); }

	C4Compiler::AnnotationContext SemanticResolver::AnnotationContextAt(const Core::Vector<const C4Compiler::AstNode*>& path, uint32 sourceId) const
	{
		return { DeclarationScope(path), sourceId };
	}

	const C4Compiler::AstNode* SemanticResolver::AnnotationNamedAt(const Core::Vector<const C4Compiler::AstNode*>& path, uint32 line, uint32 column) const
	{
		for (uint32 i = path.Count(); i-- > 0;)
		{
			if (path[i]->Type() != C4Compiler::NodeType::AnnotationItem) continue;
			const auto& item = static_cast<const C4Compiler::AnnotationItem&>(*path[i]);
			for (const C4Compiler::Token& token : item.GetNameTokens())
			{
				const C4Compiler::SourceLocation& start = token.GetStartLocation();
				const C4Compiler::SourceLocation& end = token.GetEndLocation();
				const bool after = line > start.line || (line == start.line && column >= start.column);
				const bool before = line < end.line || (line == end.line && column <= end.column);
				if (!after || !before) continue;
				const C4Compiler::DeclaredAnnotations::Overloads overloads = Annotations().Find(item.GetName().c_str(), AnnotationContextAt(path, token.GetStartLocation().sourceId));
				const C4Compiler::AnnotationSignature* likely = overloads.Empty() ? nullptr : C4Compiler::LikelyOverload(overloads, item);
				return likely ? likely->declaration : nullptr;
			}
			return nullptr; // in its arguments: what they name, not the annotation
		}
		return nullptr;
	}

	bool SemanticResolver::IsReference(const C4Compiler::AstNode& node, const C4Compiler::AstNode* parent)
	{
		switch (node.Type())
		{
		case C4Compiler::NodeType::IdentifierExpression: case C4Compiler::NodeType::MemberAccess: case C4Compiler::NodeType::StaticAccess:
			return true;
		case C4Compiler::NodeType::NamespaceQualifiedName:
			return !parent || (parent->Type() != C4Compiler::NodeType::QualType && parent->Type() != C4Compiler::NodeType::FunctionDefinition);
		default:
			return false;
		}
	}

	const C4Compiler::Type* SemanticResolver::ResolveQualType(const C4Compiler::QualTypeNode& qt,
		const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		auto cached = m_TypeCache.find(&qt);
		if (cached != m_TypeCache.end())
			return cached->second;

		uint32 unitId = UnitIdOf(qt);
		const C4Compiler::Scope* ctx = ContextFor(path).scope;
		auto r = m_Model.ResolveTypeInUnitContext(qt, unitId, ctx);
		const C4Compiler::Type* type = r.Ok() ? r.value : nullptr;
		m_TypeCache[&qt] = type;
		return type;
	}

	const C4Compiler::AstNode* SemanticResolver::DeclarationOfWrittenType(const C4Compiler::QualTypeNode& qt,
		const Core::Vector<const C4Compiler::AstNode*>& path) const
	{
		if (const C4Compiler::AstNode* declaration = Features::DeclarationOfNamedType(ResolveQualType(qt, path)))
			return declaration;
		if (!qt.HasTemplateArgs())
			return nullptr;
		std::string templateName;
		for (const C4Compiler::NamespaceQualifiedNameNode::Segment& segment : qt.BaseName()->GetSegments())
			templateName += (templateName.empty() ? "" : "::") + std::string(segment.nameToken.GetText());
		const C4Compiler::SymbolInfo* info = Lookup(templateName, UnitIdOf(qt), &path);
		return info && info->kind == C4Compiler::SymbolKind::Type ? info->declNode : nullptr;
	}

	void SemanticResolver::CollectNamespacePaths(std::unordered_set<std::string>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const C4Compiler::Scope*, std::vector<std::string>&)> walk =
			[&](const C4Compiler::Scope* scope, std::vector<std::string>& chain) {
				for (const auto& child : scope->GetChildren())
				{
					const C4Compiler::Scope* s = child.get();
					if (s->GetKind() == C4Compiler::Scope::Kind::Namespace && !s->GetName().empty())
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

	void SemanticResolver::ForEachGlobalSymbol(const std::function<void(const Core::String&, const C4Compiler::SymbolInfo&)>& visit) const
	{
		if (!m_Model.IsValid())
			return;
		Core::Vector<std::pair<Core::String, C4Compiler::SymbolInfo*>> entries;
		std::function<void(const C4Compiler::Scope*)> walk = [&](const C4Compiler::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			auto kind = scope->GetKind();
			if (kind != C4Compiler::Scope::Kind::File && kind != C4Compiler::Scope::Kind::Namespace)
				return; // Class scopes hold fields/methods - reachable only via member access, never as globals.
						// Function/block layers hold locals, never project symbols. Emitting Class-scope symbols here
						// leaked every class's members into EVERY completion list (the wrong-object bug).
			entries.Clear();
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
				if (!C4Compiler::IsExtensionMethodKey(name)) // a member of the type it extends, not a global
					visit(name, *info);
		};
		walk(m_Model.Symbols()->GetRoot());
	}

	void SemanticResolver::CollectObjectTypes(std::vector<ObjectDecl>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const C4Compiler::Scope*)> walk = [&](const C4Compiler::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			Core::Vector<std::pair<Core::String, C4Compiler::SymbolInfo*>> entries;
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
			{
				if (info->kind != C4Compiler::SymbolKind::Type || !info->type)
					continue;
				const auto* obj = dynamic_cast<const C4Compiler::ObjectType*>(C4Compiler::TypeHandle::UnwrapToValue(info->type));
				if (obj)
					out.push_back({ obj, info->declNode });
			}
		};
		walk(m_Model.Symbols()->GetRoot());
	}

	void SemanticResolver::CollectDeclarationsByName(const std::string& simpleName, std::vector<const C4Compiler::AstNode*>& out) const
	{
		if (!m_Model.IsValid())
			return;
		std::function<void(const C4Compiler::Scope*)> walk = [&](const C4Compiler::Scope* scope) {
			for (const auto& child : scope->GetChildren())
				walk(child.get());
			Core::Vector<std::pair<Core::String, C4Compiler::SymbolInfo*>> entries;
			scope->Enumerate(entries);
			for (const auto& [name, info] : entries)
				if ((name == simpleName || name == C4Compiler::ExtensionMethodKey(simpleName)) && info->declNode)
					out.push_back(info->declNode);
		};
		walk(m_Model.Symbols()->GetRoot());
	}

	void SemanticResolver::CollectFunctionDeclarations(const C4Compiler::AstNode* declaration, std::vector<const C4Compiler::AstNode*>& out) const
	{
		if (const C4Compiler::AnnotationSignature* declared = Annotations().Declared(declaration))
		{
			for (const C4Compiler::AnnotationSignature* overload : *Annotations().Named(declared->qualifiedName.c_str())) // a use names them all
				if (std::find(out.begin(), out.end(), overload->declaration) == out.end())
					out.push_back(overload->declaration);
			return;
		}
		if (m_Model.IsValid() && declaration && declaration->Type() == C4Compiler::NodeType::FunctionDefinition)
		{
			Core::Vector<const C4Compiler::FunctionRecord*> records;
			m_Model.AllFunctionRecords(records);
			for (const C4Compiler::FunctionRecord* record : records)
			{
				if (std::find(record->declarations.begin(), record->declarations.end(), declaration) == record->declarations.end())
					continue;
				for (const C4Compiler::AstNode* node : record->declarations)
					if (std::find(out.begin(), out.end(), node) == out.end())
						out.push_back(node);
			}
		}
		if (std::find(out.begin(), out.end(), declaration) == out.end())
			out.push_back(declaration);
	}
} // namespace C4LSP::Server
