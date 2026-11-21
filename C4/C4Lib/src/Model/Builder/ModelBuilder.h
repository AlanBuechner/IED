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
	struct AnnotationDeclarationNode;
	struct NamespaceNode;
	struct UnionDefinitionNode;
	struct AnnotationNode;
	struct QualTypeNode;
	struct SwizzleViewNode;

	/// The file-scope model build (docs/compiler/02-program-model.md, Build passes), exactly two passes:
	///
	///   PASS 1 - REGISTER: one walk per unit. Every file-scope declaration becomes a shell:
	///     types exist (incomplete), names exist in the scope tree, template definitions are
	///     recorded. NOTHING is resolved here - no type name is looked up against a complete
	///     definition, so declaration ORDER NEVER MATTERS (forward refs, cross-file refs, and
	///     mutual recursion all resolve in pass 2).
	///
	///   PASS 2 - RESOLVE (on demand): each pending declaration is a query asked once (DeclarationQueries):
	///     resolving one first resolves what it names - an alias's target, an enum's values, a variable's type,
	///     an object's fields - and one asked for while it is resolving depends on itself. One sweep in
	///     declaration order asks for every one.
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
		void VisitHiddenPrelude(Context& ctx, const AstNode* function);
		static std::unordered_map<uint32, uint32> ProjectOfEachUnit(const SourceManager& sources);
		void ErrorAt(const Context& ctx, const AstNode* where, const Core::String& message);
		/// "redefinition of <what> 'X': it is already a function (b.cf:12)" - what the name already is in the scope, and
		/// where, so a clash across files names both sides (BUG-291).
		Core::String Redefinition(const Context& ctx, const char* what, const Core::String& name) const;
		// The base-list and interface rules (ClassHierarchy), reported at each type's declaration.
		void CheckObjectTypes(const Context& ctx, ProgramModel& model);
		// Every annotation reflection registers folds (AnnotationCheck); the template instances they name are opened and laid out.
		void CheckAnnotations(Context& ctx, ProgramModel& model);
		void CheckTargets(const Context& ctx, const ProgramModel& model, const Core::Vector<const FunctionRecord*>& records);

private:
		struct Context
		{
			ProgramModel* model = nullptr;
			ErrorTracker* errors = nullptr;
			uint32 unitId = 0;      // the defining file for everything registered in this walk
			Scope* scope = nullptr; // current declaration scope (root, a namespace, or a class)
		};

		// --- Pass-1 pending work: shells exist, each resolves when pass 2 asks for it ----------
		/// Where a pending declaration's resolution stands. Failed is final: what the declaration names was resolved
		/// first, so asking again could not go better.
		enum class Stage : uint8 { Pending, Resolving, Resolved, Failed };
		struct Resolution
		{
			Stage stage = Stage::Pending;
			QueryResult<void> failure; // Failed: why, and where when the resolver knows
			Core::String cycle;        // Failed by depending on itself: the cycle, as what asks for this one is told
		};
		struct PendingVar { SymbolInfo* info; const VariableDefinitionNode* var; uint32 unitId; Resolution resolution; };
		// `type`: the field's type once resolved; a static member's symbols (registered in pass 1) take it then, the
		// object its field when its fields are attached in declaration order.
		struct PendingField
		{
			const ObjectType* obj;
			const VariableDefinitionNode* var;
			uint32 unitId;
			int unionRegionId = -1;
			int unionViewId = -1;
			const Type* type = nullptr;
			Resolution resolution;
		};
		// A `packed`/`unpacked` swizzle view inside an object body: becomes a PackedViewRegion (SIMD
		// <N x T>) once its element type resolves in pass 2. Lanes are accessed via FindPackedLane.
		struct PendingSwizzle { const ObjectType* obj; const SwizzleViewNode* node; uint32 unitId; AccessModifier access; Resolution resolution; }; // access: its union's
		struct PendingMethodDecl { const ObjectType* obj; const FunctionDefinitionNode* fn; uint32 unitId; Resolution resolution; };
		struct PendingFunc { Scope* scope; Core::String name; const FunctionDefinitionNode* fn; uint32 unitId; Resolution resolution; };
		struct PendingQualifiedFunc
		{
			Core::String prefix;
			Core::String last;
			const FunctionDefinitionNode* fn;
			uint32 unitId;
			Scope* declScope = nullptr;
			Resolution resolution;
		};
		struct PendingBase { const ObjectType* obj; const QualTypeNode* type; const Scope* declScope; uint32 unitId; Resolution resolution; };
		struct PendingEnum { EnumType* raw; const EnumDefinitionNode* en; uint32 unitId; const Scope* scope = nullptr; Resolution resolution; };
		// `repeats`: the alias of the same name this declaration repeats, which must name the same type (BUG-273).
		struct PendingAlias
		{
			AliasType* raw;
			const QualTypeNode* target;
			uint32 unitId;
			const Scope* scope = nullptr;
			const AliasType* repeats = nullptr;
			const AstNode* declaration = nullptr;
			Resolution resolution;
		};
		// `failedField`: the field whose type did not resolve, when one did not.
		struct PendingUnion
		{
			UnionType* raw;
			const UnionDefinitionNode* un;
			uint32 unitId;
			const Scope* scope = nullptr;
			const VariableDefinitionNode* failedField = nullptr;
			Resolution resolution;
		};

		// --- Pass 1: registration (shells only) ------------------------------------------
		void VisitUnit(Context& ctx, const AstNode* root);
		void VisitDeclaration(Context& ctx, const AstNode* node);
		void RegisterNamespace(Context& ctx, const NamespaceNode& ns);
		static const AliasType* EarlierAlias(const Scope& scope, const Core::String& name);
		/// A declaration of a C binding (what the interop pass appended to a file) that a binding has made already, the
		/// same text: two projects binding one header, or two headers sharing one (BUG-420). It is not made again, and
		/// its unit sees and reaches the first as its own.
		bool RepeatsBinding(const Context& ctx, const AstNode& declaration, const Core::String& name);
		bool IsBinding(uint32 unitId) const;
		std::string_view DeclarationText(uint32 unitId, const AstNode& declaration) const;
		void RegisterObject(Context& ctx, const ObjectDefinitionNode& obj);
		void RegisterEnum(Context& ctx, const EnumDefinitionNode& en);
		void RegisterAlias(Context& ctx, const AliasDefinitionNode& alias);
		void RegisterVariable(Context& ctx, const VariableDefinitionNode& var);
		void RegisterFunction(Context& ctx, const FunctionDefinitionNode& fn);
		void RegisterAnnotation(Context& ctx, const AnnotationDeclarationNode& declaration);
		void RegisterUnion(Context& ctx, const UnionDefinitionNode& un);
		/// A file-scope `asm` block: assembly added to the program as it is, so it takes no operands.
		void RegisterFileAssembly(Context& ctx, const AsmStatementNode& block);
		void ReportUnionMembersItCannotHold(const Context& ctx, const UnionDefinitionNode& un);
		/// A static data member is a variable of its class's scope from pass 1 on, as a global is of its namespace's:
		/// a declaration that names it finds it, and asks for its type.
		void RegisterStaticMember(Context& classCtx, const VariableDefinitionNode& member);

		// --- After pass 2: the declarations that did not resolve, each with why ------------
		struct Failure; // one unresolved declaration: what it is, where, and its resolution
		void ReportUnresolved(const Context& ctx);
		void ReportFailure(const Context& ctx, const Failure& failure);
		// An anonymous union (swizzle) inside an object body is flattened into m_PendingFields as
		// region-marked parent fields so `v.x` / `v.r` resolve and the layout shares one byte range.
		void FlattenSwizzle(Context& ctx, const ObjectType* obj, const UnionDefinitionNode& un, int regionId);
		// A field queued for pass 2: its object waits for one more part until the field is attached. A static one is a
		// variable of the class scope (`classCtx`) at once.
		void QueueField(Context& classCtx, const PendingField& field);
		void FlattenSwizzleView(Context& ctx, const ObjectType* obj, const ObjectDefinitionNode& view, int regionId, int viewId);

		// --- Pass 2 lives beside this class (Model/Builder): DeclarationQueries asks for each pending declaration
		// once and finds cycles; ModelResolver and FreeFunctionResolver say how one declaration resolves. The
		// pending lists below are their input; they are friends to read them and report through ErrorAt.
		friend class DeclarationQueries;
		friend class ModelResolver;
		friend class FreeFunctionResolver;

		ErrorTracker* m_Errors;
		const SourceManager* m_Sources = nullptr; // during Build: the units' texts, for repeated bindings

		// Pass-1 output: everything pass 2 resolves, in declaration order. A list keeps its entries (their places are
		// what DeclarationQueries indexes); each entry's resolution says where it stands.
		Core::Vector<PendingVar> m_PendingVars;
		Core::Vector<PendingField> m_PendingFields;
		Core::Vector<PendingSwizzle> m_PendingSwizzles;
		Core::Vector<PendingMethodDecl> m_PendingMethodDecls;
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
