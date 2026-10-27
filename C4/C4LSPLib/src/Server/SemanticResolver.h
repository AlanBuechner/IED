#pragma once
#include "Core/Core.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include "Model/Types.h"
#include "Parser/AstNode.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace C4LSP::Server
{
	/// The resolved facts for one reference node (an identifier / member access / static access).
	/// Replaces the per-node entries the old analyzer used to pre-compute in
	/// ProgramModel::ReferenceDeclarations() and ExpressionTypes().
	struct NodeResolution
	{
		const Scripting::Type* type = nullptr;           // resolved value type (or the type, for a type name)
		const Scripting::AstNode* declaration = nullptr; // declaring AST node (null when unresolved / built-in)
		bool isFunction = false;                          // denotes a function or method
	};

	/// LSP-side semantic resolution. The re-architecture deleted the per-node maps the old analyzer
	/// pre-computed (expressionTypes / referenceDeclarations / resolvedQualTypes) and the flat symbol-
	/// table API, moving resolution into "re-derive while lowering" through the model's query verbs. This
	/// class re-derives a node's facts by driving THOSE SAME verbs - FindVisibleQualified,
	/// ResolveTypeInUnitContext, QueryVariable, QueryMember, QueryFunction - with an AST-path-derived
	/// context and a lexical local scan, exactly the resolution HIR lowering performs. It owns no name-
	/// resolution logic of its own, so LSP facts can never diverge from what the compiler resolves (the
	/// compiler is the source of truth).
	class SemanticResolver
	{
	public:
		explicit SemanticResolver(const Scripting::ProgramModel& model) : m_Model(model) {}

		/// Clear resolution caches. Called after every Recompile() so stale node facts are dropped.
		void Invalidate() { m_RefCache.clear(); m_TypeCache.clear(); m_RecordByDeclaration.clear(); m_RecordsIndexed = false; }

		/// Resolve a reference node to its type + declaring declaration, given the enclosing AST path
		/// (outermost first, innermost last) that contains it. The path supplies the function-local
		/// declarations visible at the node and the enclosing type/scope context.
		NodeResolution ResolveReference(const Scripting::AstNode& node, const Core::Vector<const Scripting::AstNode*>& path) const;

		/// Resolve a bare identifier NAME at a position (no AST node available yet - e.g. completion while
		/// typing) using the same locals-then-model resolution as an IdentifierExpression reference.
		NodeResolution ResolveIdentifier(const std::string& name, const Core::Vector<const Scripting::AstNode*>& path) const;

		/// Resolve a QualTypeNode to its concrete type in the context of its enclosing declarations.
		const Scripting::Type* ResolveQualType(const Scripting::QualTypeNode& qt, const Core::Vector<const Scripting::AstNode*>& path) const;

		/// A simple or qualified name -> SymbolInfo (visibility-aware). The new LookupQualified/Lookup.
		const Scripting::SymbolInfo* Lookup(const std::string& name, uint32 unitId, const Core::Vector<const Scripting::AstNode*>* path = nullptr) const;

		/// Every namespace path in the program ("Win", "Win::Test"), outermost-first joined with "::".
		void CollectNamespacePaths(std::unordered_set<std::string>& out) const;

		struct GlobalSymbol { std::string name; Scripting::SymbolKind kind = Scripting::SymbolKind::Type; const Scripting::Type* type = nullptr; const Scripting::FunctionRecord* fn = nullptr; };
		/// Every symbol in File/Namespace/Class scopes (globals + static members, never function/block locals).
		void CollectGlobalSymbols(std::vector<GlobalSymbol>& out) const;

		struct ObjectDecl { const Scripting::ObjectType* object = nullptr; const Scripting::AstNode* decl = nullptr; };
		/// Every user-defined object type in the program with its declaring node (implementation / completion).
		void CollectObjectTypes(std::vector<ObjectDecl>& out) const;

		/// Every declaration node whose simple name matches, project-wide (by-name navigation fallback).
		void CollectDeclarationsByName(const std::string& simpleName, std::vector<const Scripting::AstNode*>& out) const;

		/// Every node that declares or defines the same function as `declaration` (its declaration in a type or a
		/// header and its out-of-line definition), in source order; just `declaration` for anything else.
		void CollectFunctionDeclarations(const Scripting::AstNode* declaration, std::vector<const Scripting::AstNode*>& out) const;

		/// The object type a path is inside - a member function's own type, what an implicit `this` names; null outside one.
		const Scripting::ObjectType* EnclosingObject(const Core::Vector<const Scripting::AstNode*>& path) const;

	private:
		struct Ctx { const Scripting::ObjectType* enclosingType = nullptr; const Scripting::Scope* scope = nullptr; uint32 unitId = 0; };
		Ctx ContextFor(const Core::Vector<const Scripting::AstNode*>& path) const;
		const Scripting::Scope* DeclarationScope(const Core::Vector<const Scripting::AstNode*>& path) const;
		/// The record of the function `path[fi]` declares or defines (see the .cpp); null when the model has none.
		const Scripting::FunctionRecord* RecordOf(const Core::Vector<const Scripting::AstNode*>& path, int fi, uint32 unitId) const;
		/// The first record whose declarations hold the function node `fn`; null when none does.
		const Scripting::FunctionRecord* RecordDeclaring(const Scripting::AstNode& fn) const;
		bool ResolveLocal(const std::string& name, const Core::Vector<const Scripting::AstNode*>& path,
			const Scripting::AstNode*& declOut, const Scripting::Type*& typeOut) const;
		const Scripting::Type* DeclaredType(const Scripting::AstNode& declaration, const Core::Vector<const Scripting::AstNode*>& path) const;
		const Scripting::SymbolInfo* TypeNamedBy(const Scripting::AstNode& base, const Core::Vector<const Scripting::AstNode*>& path) const;

		const Scripting::ProgramModel& m_Model;
		mutable std::unordered_map<const Scripting::AstNode*, NodeResolution> m_RefCache;
		mutable std::unordered_map<const Scripting::QualTypeNode*, const Scripting::Type*> m_TypeCache;
		mutable std::unordered_map<const Scripting::AstNode*, const Scripting::FunctionRecord*> m_RecordByDeclaration;
		mutable bool m_RecordsIndexed = false;
	};
} // namespace C4LSP::Server
