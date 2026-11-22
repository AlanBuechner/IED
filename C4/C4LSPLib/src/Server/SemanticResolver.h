#pragma once
#include "Core/Core.h"
#include "Compiler/CompilerInstance.h"
#include "Model/DeclaredAnnotations.h"
#include "Model/ProgramModel.h"
#include "Model/SymbolTable.h"
#include <functional>
#include <memory>
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
		const C4Compiler::Type* type = nullptr;           // resolved value type (or the type, for a type name)
		const C4Compiler::AstNode* declaration = nullptr; // declaring AST node (null when unresolved / built-in)
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
		explicit SemanticResolver(const C4Compiler::ProgramModel& model) : m_Model(model) {}

		/// Clear resolution caches. Called after every Recompile() so stale node facts are dropped.
		void Invalidate() { m_RefCache.clear(); m_TypeCache.clear(); m_RecordByDeclaration.clear(); m_RecordsIndexed = false; }

		/// The annotations the program declares (`annotation Requires(typename component) on type;`), read by the model.
		const C4Compiler::DeclaredAnnotations& Annotations() const;
		/// The declaration of the annotation whose name is at `position` (`Requires` of `[Requires(Mesh)]`): the overload its
		/// arguments take, else the first; null off an annotation's name, and for a name nothing declares.
		const C4Compiler::AstNode* AnnotationNamedAt(const Core::Vector<const C4Compiler::AstNode*>& path, uint32 line, uint32 column) const;
		/// Where an annotation written at the end of `path`, in the file `sourceId`, is looked up from, as the model looks it
		/// up (docs/language/12 section 4.2): the type it is in, else its namespace.
		C4Compiler::AnnotationContext AnnotationContextAt(const Core::Vector<const C4Compiler::AstNode*>& path, uint32 sourceId) const;

		/// Resolve a reference node to its type + declaring declaration, given the enclosing AST path
		/// (outermost first, innermost last) that contains it. The path supplies the function-local
		/// declarations visible at the node and the enclosing type/scope context.
		NodeResolution ResolveReference(const C4Compiler::AstNode& node, const Core::Vector<const C4Compiler::AstNode*>& path) const;

		/// Whether `node` (inside `parent`, null at the root) names something where code uses it: an identifier, a member or
		/// static access, or a qualified name with template arguments written as a value (`Box<int32>{1}`, `[Holds(Box<int32>)]`)
		/// - not the name a function declaration declares or a type node is made of.
		static bool IsReference(const C4Compiler::AstNode& node, const C4Compiler::AstNode* parent);

		/// Resolve a bare identifier NAME at a position (no AST node available yet - e.g. completion while
		/// typing) using the same locals-then-model resolution as an IdentifierExpression reference.
		NodeResolution ResolveIdentifier(const std::string& name, const Core::Vector<const C4Compiler::AstNode*>& path) const;

		/// Resolve a QualTypeNode to its concrete type in the context of its enclosing declarations.
		const C4Compiler::Type* ResolveQualType(const C4Compiler::QualTypeNode& qt, const Core::Vector<const C4Compiler::AstNode*>& path) const;

		/// The declaration of the type a QualTypeNode names: its resolved type's, else the template's when it specializes
		/// one by a parameter still unbound (`Ref` of `Ref<T>` inside `template<T>`, which has no type, BUG-473).
		const C4Compiler::AstNode* DeclarationOfWrittenType(const C4Compiler::QualTypeNode& qt, const Core::Vector<const C4Compiler::AstNode*>& path) const;

		/// A simple or qualified name -> SymbolInfo (visibility-aware). The new LookupQualified/Lookup.
		const C4Compiler::SymbolInfo* Lookup(const std::string& name, uint32 unitId, const Core::Vector<const C4Compiler::AstNode*>* path = nullptr) const;

		/// Every namespace path in the program ("Win", "Win::Test"), outermost-first joined with "::".
		void CollectNamespacePaths(std::unordered_set<std::string>& out) const;

		/// Visits every symbol of the file and namespace scopes, with its name (never a class's members nor a function's
		/// locals).
		void ForEachGlobalSymbol(const std::function<void(const Core::String&, const C4Compiler::SymbolInfo&)>& visit) const;

		struct ObjectDecl { const C4Compiler::ObjectType* object = nullptr; const C4Compiler::AstNode* decl = nullptr; };
		/// Every user-defined object type in the program with its declaring node (implementation / completion).
		void CollectObjectTypes(std::vector<ObjectDecl>& out) const;

		/// Every declaration node whose simple name matches, project-wide (by-name navigation fallback).
		void CollectDeclarationsByName(const std::string& simpleName, std::vector<const C4Compiler::AstNode*>& out) const;

		/// Every node that declares or defines the same function as `declaration` (its declaration in a type or a
		/// header and its out-of-line definition), in source order; just `declaration` for anything else.
		void CollectFunctionDeclarations(const C4Compiler::AstNode* declaration, std::vector<const C4Compiler::AstNode*>& out) const;

		/// The object type a path is inside - a member function's own type, what an implicit `this` names; null outside one.
		const C4Compiler::ObjectType* EnclosingObject(const Core::Vector<const C4Compiler::AstNode*>& path) const;
		/// The type whose members a path reaches without naming an object, as the compiler resolves its names there: the
		/// method's own type in its body, defined in the type or out of it (`void Player::OnUpdate() { ... }`); else the
		/// type the path is inside. Null outside any.
		const C4Compiler::ObjectType* ImplicitThis(const Core::Vector<const C4Compiler::AstNode*>& path) const { return ContextFor(path).enclosingType; }

	private:
		struct Ctx { const C4Compiler::ObjectType* enclosingType = nullptr; const C4Compiler::Scope* scope = nullptr; uint32 unitId = 0; };
		Ctx ContextFor(const Core::Vector<const C4Compiler::AstNode*>& path) const;
		const C4Compiler::Scope* DeclarationScope(const Core::Vector<const C4Compiler::AstNode*>& path) const;
		/// The record of the function `path[fi]` declares or defines (see the .cpp); null when the model has none.
		const C4Compiler::FunctionRecord* RecordOf(const Core::Vector<const C4Compiler::AstNode*>& path, int fi, uint32 unitId) const;
		/// The first record whose declarations hold the function node `fn`; null when none does.
		const C4Compiler::FunctionRecord* RecordDeclaring(const C4Compiler::AstNode& fn) const;
		bool ResolveLocal(const std::string& name, const Core::Vector<const C4Compiler::AstNode*>& path,
			const C4Compiler::AstNode*& declOut, const C4Compiler::Type*& typeOut) const;
		const C4Compiler::Type* DeclaredType(const C4Compiler::AstNode& declaration, const Core::Vector<const C4Compiler::AstNode*>& path) const;
		const C4Compiler::SymbolInfo* TypeNamedBy(const C4Compiler::AstNode& base, const Core::Vector<const C4Compiler::AstNode*>& path) const;

		const C4Compiler::ProgramModel& m_Model;
		mutable std::unordered_map<const C4Compiler::AstNode*, NodeResolution> m_RefCache;
		mutable std::unordered_map<const C4Compiler::QualTypeNode*, const C4Compiler::Type*> m_TypeCache;
		mutable std::unordered_map<const C4Compiler::AstNode*, const C4Compiler::FunctionRecord*> m_RecordByDeclaration;
		mutable bool m_RecordsIndexed = false;
	};
} // namespace C4LSP::Server
