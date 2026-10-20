#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	// =====================================================================
	// TYPE RESOLVER (doc 04) - the subsystem behind every type position in the AST. One entry point,
	// everything resolved: builtin name, alias (transparent - same instance), qualified path, template
	// specialization (instantiated here via TemplateInstantiator, cached, indistinguishable from any
	// hand-written class), and trailing indirections (const T*, T&, T[N]). It is friended into
	// ProgramModel so it can reach the symbol table, type store, and instantiation caches. The caller
	// never sees a symbol table or a template definition - and never knows a template exists.
	// =====================================================================
	class TypeResolver
	{
	public:
		/// Public query entry (doc 04): resolve a type as written, in unitId's visibility domain.
		QueryResult<const Type*> ResolveType(const ProgramModel& model, const QualTypeNode& node, uint32 unitId) const;

		/// Pass-2 resolution entry (doc 02): resolve a type as written in the defining file's scope.
		/// BUILD-TIME resolution is program-wide (unitId 0 = sees everything) so the model is complete
		/// and valid for the whole program regardless of include order.
		QueryResult<const Type*> ResolveTypeInContext(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, const Scope* context) const;

		/// Lowering-time entry: resolve a type as written in unitId's include-closure visibility AND
		/// `context`'s scope (the function's enclosing namespace). Unlike ResolveTypeInContext (pass 2,
		/// program-wide unitId 0), this keeps the real unitId so per-unit include-closure still applies,
		/// while an unqualified name declared in the function's namespace is found. This is what makes
		/// `Counter a;` inside `namespace ExtNs { struct Counter {...} int32 f(){...} }` lower correctly.
		QueryResult<const Type*> ResolveTypeInUnitContext(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, const Scope* context) const;

		/// The full internal walk: qualified segments + template specialization + indirections.
		/// `bindingScope` (instantiation context) is checked FIRST so a template parameter name binds to
		/// its concrete type before any global lookup - this is the whole of template instantiation:
		/// fake aliases in a scope, nothing else.
		QueryResult<const Type*> ResolveTypeInternal(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, Scope* bindingScope, const Scope* context) const;
		static const Scope* UnqualifiedLookupStart(const ProgramModel& model, const Scope* bindingScope, const Scope* context);
		QueryResult<const Type*> InstantiateSegment(const ProgramModel& model, const NamespaceQualifiedNameNode::Segment& segment,
			const Scope* templateFrom, uint32 unitId, Scope* bindingScope, const Scope* context) const;

		/// Trailing indirections (const T*, T&, T[N]) on top of a resolved base type. Array sizes may
		/// name a template non-type parameter and are folded against the binding layer.
		/// The error for a `packed` type whose first layer is not a sized array (D-36).
		static Core::String PackedWithoutLanes(const QualTypeNode& node, const Type* base);
		QueryResult<const Type*> ApplyIndirections(const ProgramModel& model, const Type* baseType, const QualTypeNode& node, uint32 unitId,
			Scope* bindingScope, const Scope* context) const;
		/// A return list `[float, bool]`: its elements resolved, then its struct (D-34).
		QueryResult<const Type*> ResolveReturnList(const ProgramModel& model, const QualTypeNode& node, uint32 unitId, Scope* bindingScope,
			const Scope* context) const;

		/// Name form of the same resolution: "Point", "Math::Vec". Aliases are transparent (the SAME
		/// instance). A template name WITHOUT its arguments is not a type (NotFound with an actionable hint).
		QueryResult<const Type*> QueryType(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* scopeContext = nullptr) const;

	private:
		friend class ProgramModel; // the only caller (public verbs delegate here)

		/// Visibility-aware qualified walk: "Math::Vec", "S::Inner" (nested types descend into the
		/// class scope), each segment checked against the asking unit's include closure. Returns
		/// nullptr when any segment is missing or not visible. Every name query goes through this.
		const SymbolInfo* WalkQualified(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* scope) const;
		const SymbolInfo* FindVisibleQualified(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* context = nullptr) const;
	};
} // namespace Scripting
