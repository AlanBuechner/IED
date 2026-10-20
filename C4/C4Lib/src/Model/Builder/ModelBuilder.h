#pragma once
#include "Model/ProgramModel.h"
#include "Compiler/ErrorTracker.h"
#include "Core/Core.h"
#include <functional>

namespace Scripting
{
	class CompilerInstance;
	class SourceManager;
	struct ObjectDefinitionNode;
	struct FunctionDefinitionNode;
	struct VariableDefinitionNode;
	struct EnumDefinitionNode;
	struct AliasDefinitionNode;
	struct NamespaceNode;
	struct UnionDefinitionNode;
	struct AnnotationNode;
	struct QualTypeNode;
	struct SwizzleViewNode;

	/// The file-scope model build (doc 02/03), exactly two passes:
	///
	///   PASS 1 - REGISTER: one walk per unit. Every file-scope declaration becomes a shell:
	///     types exist (incomplete), names exist in the scope tree, template definitions are
	///     recorded. NOTHING is resolved here - no type name is looked up against a complete
	///     definition, so declaration ORDER NEVER MATTERS (forward refs, cross-file refs, and
	///     mutual recursion all resolve in pass 2).
	///
	///   PASS 2 - RESOLVE (fixed point): now that every shell exists, resolve field types, base
	///     classes, method signatures, enum values, alias targets, global types, and fold
	///     constants - iterating until nothing changes. A name resolves because the thing it
	///     names was registered in pass 1, regardless of where it was written.
	///
	/// The builder never enters a function body: bodies stay as AST for HIR lowering (doc 06).
	/// Out-of-line definitions (`Str::FromCStr`, `template<T> void Vector::Init`) are matched to
	/// their in-class declarations by name + resolved signature, and the body is attached to the
	/// record - a declaration and its definition become ONE symbol.
	class ModelBuilder
	{
		struct Context; // pass context (defined below); forward-declared for the public Error()

	public:
		explicit ModelBuilder(ErrorTracker* errors);

		/// Build the model from a compiler instance (after CompileAll). Pass a fresh ProgramModel
		/// for a fresh build.
		void Build(CompilerInstance& compiler, ProgramModel& model);

		/// The prelude's declarations, after the program's (D-43).
		void VisitPrelude(Context& ctx, CompilerInstance& compiler);
		static std::unordered_map<uint32, uint32> ProjectOfEachUnit(const SourceManager& sources);
		void ErrorAt(const Context& ctx, const AstNode* where, const Core::String& message);
		/// `: <reason>` when `type` (a QualTypeNode) does not resolve in `scope`, else empty.
		Core::String Why(const Context& ctx, const AstNode* type, uint32 unitId, const Scope* scope) const;
		void ReportUnresolved(const Context& ctx, const AstNode* declaration, const Core::String& what, const AstNode* type, uint32 unitId,
			const Scope* scope);
		Core::String SignatureWhy(const Context& ctx, const FunctionDefinitionNode& fn, uint32 unitId, const Scope* scope) const;
		// The base-list and interface rules (ClassHierarchy), reported at each type's declaration.
		void CheckObjectTypes(const Context& ctx, ProgramModel& model);

private:
		struct Context
		{
			ProgramModel* model = nullptr;
			ErrorTracker* errors = nullptr;
			uint32 unitId = 0;      // the defining file for everything registered in this walk
			Scope* scope = nullptr; // current declaration scope (root, a namespace, or a class)
		};

		// --- Pass-1 pending work: shells exist, resolution is deferred to pass 2 ----------
		struct PendingVar { SymbolInfo* info; const VariableDefinitionNode* var; uint32 unitId; };
		struct PendingField { const ObjectType* obj; const VariableDefinitionNode* var; uint32 unitId; int unionRegionId = -1; int unionViewId = -1; };
	// A `packed`/`unpacked` swizzle view inside an object body: becomes a PackedViewRegion (SIMD
	// <N x T>) once its element type resolves in pass 2. Lanes are accessed via FindPackedLane.
	struct PendingSwizzle { const ObjectType* obj; const SwizzleViewNode* node; uint32 unitId; };
		struct PendingMethodDecl { const ObjectType* obj; const FunctionDefinitionNode* fn; uint32 unitId; };
		struct PendingMethodDef
		{
			Core::String className;   // first segment of the qualified name
			Core::String methodName;  // last segment
			const FunctionDefinitionNode* fn;
			uint32 unitId;
		};
		struct PendingFunc { Scope* scope; Core::String name; const FunctionDefinitionNode* fn; uint32 unitId; };
		struct PendingQualifiedFunc { Core::String prefix; Core::String last; const FunctionDefinitionNode* fn; uint32 unitId; Scope* declScope = nullptr; };
		struct PendingBase { const ObjectType* obj; const QualTypeNode* type; const Scope* declScope; uint32 unitId; };
		struct PendingEnum { EnumType* raw; const EnumDefinitionNode* en; uint32 unitId; const Scope* scope = nullptr; Core::String why; };
		struct PendingAlias { AliasType* raw; const QualTypeNode* target; uint32 unitId; const Scope* scope = nullptr; };
		struct PendingUnion { UnionType* raw; const UnionDefinitionNode* un; uint32 unitId; const Scope* scope = nullptr; };

		// --- Pass 1: registration (shells only) ------------------------------------------
		void VisitUnit(Context& ctx, const AstNode* root);
		void VisitDeclaration(Context& ctx, const AstNode* node);
		void RegisterNamespace(Context& ctx, const NamespaceNode& ns);
		void RegisterObject(Context& ctx, const ObjectDefinitionNode& obj);
		void RegisterEnum(Context& ctx, const EnumDefinitionNode& en);
		void RegisterAlias(Context& ctx, const AliasDefinitionNode& alias);
		void RegisterVariable(Context& ctx, const VariableDefinitionNode& var);
		void RegisterFunction(Context& ctx, const FunctionDefinitionNode& fn);
		void RegisterUnion(Context& ctx, const UnionDefinitionNode& un);
		void ReportUnionMembersItCannotHold(const Context& ctx, const UnionDefinitionNode& un);
		void ReportUnresolvedUnion(const Context& ctx, const PendingUnion& u);
		Core::String QualifiedWhy(const Context& ctx, const PendingQualifiedFunc& f) const;
		// An anonymous union (swizzle) inside an object body is flattened into m_PendingFields as
		// region-marked parent fields so `v.x` / `v.r` resolve and the layout shares one byte range.
		void FlattenSwizzle(Context& ctx, const ObjectType* obj, const UnionDefinitionNode& un, int regionId);
		void FlattenSwizzleView(Context& ctx, const ObjectType* obj, const ObjectDefinitionNode& view, int regionId, int viewId);

		// --- Pass 2 lives in the ModelResolver subsystem (Model/ModelResolver.{h,cpp}) --------
		// The pending work-lists below are its input; Build() drives it. It is friended here so it can
		// read them and report through Error(). One header, one implementation each: this file owns
		// pass 1 (registration) + the build driver; ModelResolver owns pass 2 (fixed-point resolution).
		friend class ModelResolver;
		friend class FreeFunctionResolver; // pass-2 free-function resolver reads the pending work-lists

		ErrorTracker* m_Errors;

		// Pass-1 output: everything pass 2 must resolve. Items are removed as they settle.
		Core::Vector<PendingVar> m_PendingVars;
		Core::Vector<PendingField> m_PendingFields;
		Core::Vector<PendingSwizzle> m_PendingSwizzles;
		Core::Vector<PendingMethodDecl> m_PendingMethodDecls;
		Core::Vector<PendingMethodDef> m_PendingMethodDefs;
		Core::Vector<PendingFunc> m_PendingFuncs;
		Core::Vector<PendingQualifiedFunc> m_PendingQualifiedFuncs;
		Core::Vector<PendingBase> m_PendingBases;
		Core::Vector<PendingEnum> m_PendingEnums;
		Core::Vector<PendingAlias> m_PendingAliases;
		Core::Vector<PendingUnion> m_PendingUnions;
	};

	/// A named union's fields in declaration order: each plain member (view -1) and the fields of each struct view
	/// (`struct { float x; float y; };`, views 0, 1, ...).
	void ForEachUnionField(const UnionDefinitionNode& un, const std::function<void(const VariableDefinitionNode&, int view)>& visit);

} // namespace Scripting
