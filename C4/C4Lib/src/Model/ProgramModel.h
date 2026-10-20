#pragma once
#include <map>
#include "Model/TestRegistry.h"
#include <vector>
#include <deque>
#include "Model/Types.h"
#include "Model/SymbolTable.h"
#include "Model/TypeStore.h"
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Model/OverloadResolution.h"
#include "Compiler/CpuTarget.h"
#include "Compiler/Projects.h"
#include <unordered_map>
#include "Model/Closures.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Core/Core.h"
#include <memory>
#include <unordered_map>
#include <tuple>

namespace Scripting
{
	struct AsmStatementNode;
	class TemplateDefinitionNode; // template parameter list (parser node)
	class ObjectDefinitionNode;    // object template body
	class FunctionDefinitionNode;  // function template body
	class EnumDefinitionNode;      // enum definition (local types, doc 06)
	struct AliasDefinitionNode;    // local alias (doc 06)
	struct AstNode;                // any declaration node (RegisterLocalType)
	/// Who is asking, and from what type scope. This is the ONLY context a query needs:
	///  - unitId selects the visibility domain (include closure, doc 09).
	///  - enclosingType is the type whose members are in scope for UNQUALIFIED names: when
	///    lowering `DoThing()` inside a member function of S, this is S. One query then answers
	///    "member of S? static of S (or an enclosing nested type)? free function?" by walking the
	///    symbol tree from S's class scope outward - and a member of S::Inner also sees the statics
	///    of S because Inner's class scope nests inside S's.
	/// How the object a method is called on may be used (D-31): a read-only one calls only const methods.
	enum class ReceiverAccess : uint8 { Writable, ReadOnly };

	struct LookupContext
	{
		uint32 unitId = 0;                  // 0 = builtin context, sees everything
		const ObjectType* enclosingType = nullptr;
		const Scope* scope = nullptr;       // the function's enclosing namespace/class: unqualified names resolve from here (walking up), not root
		ReceiverAccess receiver = ReceiverAccess::Writable; // the object of a method call, the left operand of an operator
		ReceiverAccess operand = ReceiverAccess::Writable;  // the right operand of an operator whose method is called on it
	};

	/// What a variable name denotes (doc 04: one verb for local / member / global / static).
	struct VariableInfo
	{
		const Type* type = nullptr;
		bool isConst = false;
		bool isStatic = false;
		const SymbolInfo* symbol = nullptr; // the declaring record (LSP/DAP, diagnostics); null for fields
		ConstantValue constValue;           // isConst: the folded value (a constant has no addressable storage)
	};

	/// A member found on a type: a data field or a method (doc 04). Most-derived first.
	struct MemberBinding
	{
		bool IsField = false;
		const ObjectType::FieldInfo* field = nullptr;
		const ObjectType::MethodInfo* method = nullptr;
		const ObjectType* owner = nullptr; // the type that declares the member
		int64 ownerOffset = 0;             // where the owner's sub-object sits inside the queried type
	};

	/// A resolved operator (D-25: the left operand is always the receiver): the method's record, its
	/// MethodInfo when the receiver is an object (null for an extension on a primitive), and whether it is
	/// the in-place `<op>_assign` form or a plain-op fallback for a compound assignment.
	struct OperatorBinding
	{
		const ObjectType::MethodInfo* method = nullptr;
		const FunctionRecord* record = nullptr; // the method's lowering record
		bool isAssignForm = false;     // true when it IS the in-place form, false for fallback
	};

	/// The template parameters an instantiation was bound to. HIR lowering of a template body
	/// pushes a scope and registers these as FAKE ALIASES (T := int32) so `T` resolves through
	/// ordinary name lookup - no AST rewriting, ever. Popping the scope removes them: they cannot leak.
	struct InstantiationBindings
	{
		Core::Vector<std::pair<Core::String, const Type*>> typeParams;      // T := int32
		Core::Vector<std::pair<Core::String, ConstantValue>> valueParams;   // N := 4 (non-type)
		Core::Vector<std::pair<Core::String, SymbolInfo>> packParams; // Ts := int32, float (D-19)
	};

	/// The single object every phase holds and queries (doc 03). It OWNS all type/symbol/constant
	/// facts - a phase never constructs a type or a scope layer, it asks. Queries take SOURCE-LEVEL
	/// inputs (the AST node as written, a name, an operand type) and do everything internally:
	/// alias unwrap, template instantiation, overload ranking, visibility. The caller never sees
	/// a symbol table, a template definition, or a candidate list - and never knows a template exists.
	class ProgramModel
	{
	public:
		ProgramModel();
		~ProgramModel();

		// A model is move-only (it owns its data); phases hold a reference to the one shared model.
		ProgramModel(const ProgramModel&) = delete;
		ProgramModel& operator=(const ProgramModel&) = delete;
		ProgramModel(ProgramModel&& other) noexcept;
		ProgramModel& operator=(ProgramModel&& other) noexcept;

		bool IsValid() const { return m_Valid; }

		/// Mark the model queryable (doc 02): called by the builder after pass 1 has registered
		/// every shell, before pass 2 resolves them. Queries then work throughout resolution and
		/// after - a phase never distinguishes "resolving" from "resolved".
		void BeginResolution() { m_Valid = true; }

		// ===========================================================================
		// TYPE QUERIES (doc 04). Input is the type AS WRITTEN - a QualTypeNode from the AST.
		// One call, everything resolved: builtin name, alias (transparent - same instance),
		/// qualified path, template specialization (instantiated here, cached, indistinguishable
		// from any hand-written class), and trailing indirections (const T*, T&, T[N]).
		// ===========================================================================
		QueryResult<const Type*> ResolveType(const QualTypeNode& node, uint32 unitId = 0) const;
		/// Lowering-time resolution: unit include-closure AND the function's enclosing scope (namespace),
		/// so an unqualified name declared in that namespace resolves at lowering time.
		QueryResult<const Type*> ResolveTypeInUnitContext(const QualTypeNode& node, uint32 unitId, const Scope* context) const;

		/// Name form of the same resolution: "Point", "Math::Vec". A template name WITHOUT its
		/// arguments is not a type (NotFound with an actionable hint).
		QueryResult<const Type*> QueryType(const Core::String& name, uint32 unitId = 0, const Scope* scopeContext = nullptr) const;

		/// The class scope that holds an object type's members: its fields and static data members
		/// (a static field registers a backing Variable symbol here). Null for a non-object type.
		const Scope* TypeScope(const ObjectType* obj) const { auto it = m_TypeScopes.find(obj); return it != m_TypeScopes.end() ? it->second : nullptr; }
		/// The type's name as symbols spell it, the same in every program that has the type (an instance's names its
		/// arguments): what compiler-written functions and tables of the type are named after.
		Core::String TypeSymbol(const ObjectType* type) const;

		// ===========================================================================
		// FUNCTION QUERIES (doc 08): ONE verb for free functions, methods, operators-by-name,
		/// and constructors. `name` is the source-level name ("Fa", "Math::AddX", "operator_add");
		/// `receiver` null = unqualified name; non-null = "must exist on this type"
		/// (`someVar.DoThing()`). Overloads are resolved HERE against argTypes - the caller gets
		/// back exactly ONE signature (or Ambiguous / NotFound), never a candidate list. A method's
		/// implicit `this` is NOT in argTypes; it is implied by receiver. tmplArgs: the explicit
		// `<...>` node from the call site, or nullptr; function templates instantiate transparently
		// and cache like type specializations.
		// ===========================================================================
		/// `name` written as a value without template arguments where it names function templates (D-17): the
		/// type that binds it to a funcptr or func parameter, whose signature picks the instance; null otherwise.
		const TemplateNameType* QueryTemplateName(const Core::String& name, uint32 unitId, const Scope* scope) const;
		QueryResult<const FunctionType*> QueryFunction(const Core::String& name,
			const Type* receiver = nullptr, const TemplateArgumentListNode* tmplArgs = nullptr,
			const Core::Vector<const Type*>& argTypes = {}, const LookupContext& ctx = {},
			const FunctionRecord** outRecord = nullptr) const;

		/// The lowering record for a resolved signature: body node, mangled symbol, containing
		/// type. QueryFunction returns the signature (what a call needs); this returns the symbol
		/// (what emission needs). NOTE: the interned FunctionType is shared by every function with the
		/// same signature, so this map is only unique for single-declaration signatures - callers that
		/// must name a SPECIFIC function take `outRecord` from QueryFunction instead.
		const FunctionRecord* RecordFor(const FunctionType* sig) const;

		/// The lowering record for a method found by QueryMember/QueryOperator (a MethodInfo on an
		/// object type). Method order in the type's method list is stable and IS the index into that
		/// type's parallel record list, so this is an exact lookup - no re-resolution needed. Null
		/// when the method has no body yet (declaration only) or is not on `type`.
		const FunctionRecord* RecordForMethod(const ObjectType* type, const ObjectType::MethodInfo* method) const;
		/// The method a method record declares in its type; null for a free function or an extension method.
		const ObjectType::MethodInfo* MethodForRecord(const FunctionRecord& record) const;
		/// The extension methods whose receiver is the primitive `type` (`... extends float`); null when none.
		const Core::Vector<const FunctionRecord*>* PrimitiveExtensions(const Type* type) const
		{
			auto it = m_PrimitiveExtensions.find(type);
			return it != m_PrimitiveExtensions.end() ? &it->second : nullptr;
		}

		/// The compiler-written lifecycle function of `type` (docs/language/06): created on first request,
		/// owned by the model, lowered by HIR's Lifecycle. Create and Delete take no argument; Copy takes
		/// `const T& other`. Lookup returns null when it was never requested.
		const FunctionRecord* EnsureSynthesizedLifecycle(const ObjectType* type, LifecycleOp op) const;
		const FunctionRecord* SynthesizedLifecycle(const ObjectType* type, LifecycleOp op) const;

		/// Operator form: `a + b`. The TOKEN as written from the AST (or an already-canonical name
		/// like "add") selects the operator; a null rhs makes it unary. Resolves to a method on lhs
		/// or rhs (left first), in-place form preferred for compound assignments with a plain-op
		/// fallback. `a.operator_add(b)` needs no special path: it is QueryFunction("operator_add",
		/// receiver=a.type, ...) and the operator_annotations set makes that name resolve to
		// `[operator("add")]` - including custom operators like `operator_hash` for `[operator("hash")]`.
		QueryResult<OperatorBinding> QueryOperator(const Core::String& opToken, const Type* lhs,
			const Type* rhs, const LookupContext& ctx = {}) const;

		// ===========================================================================
		// VARIABLE QUERIES (doc 04): ONE verb for local / member / global / static.
		// receiver null + ctx.enclosingType set answers "member of the enclosing type? global?"
		/// in that order - exactly what an unqualified `x` inside a member function is.
		// ===========================================================================
		QueryResult<VariableInfo> QueryVariable(const Core::String& name,
			const Type* receiver = nullptr, const LookupContext& ctx = {}) const;

		/// A member by name on a type (fields and methods together), most-derived first.
		QueryResult<MemberBinding> QueryMember(const Type* base, const Core::String& name) const;

		// ===========================================================================
		// SYMBOL / CONSTANT / LAYOUT QUERIES
		// ===========================================================================
		QueryResult<const SymbolInfo*> QuerySymbol(const Core::String& name, uint32 unitId = 0, const Scope* context = nullptr) const;
		/// A constant by (possibly qualified) name: a const variable's folded value or an enum
		/// member ("Color::Green", "Math::K"). Cyclic const references report CyclicDependency.
		QueryResult<ConstantValue> QueryConstant(const Core::String& name, uint32 unitId = 0, const Scope* context = nullptr) const;
		QueryResult<size_t> SizeOf(const Type* t) const;   // incomplete (forward-declared) types: Ok with 0
		QueryResult<size_t> AlignOf(const Type* t) const;
		const Type* ElementOf(const Type* t) const;        // array element / pointer pointee, else nullptr

		// ===========================================================================
		// TEMPLATE INSTANTIATION. The main path is transparent: ResolveType/QueryFunction
		/// instantiate on the spot and the caller never knows. This verb exists for callers that
		/// hold a specialization node directly (tests, LSP "go to definition") and want the
		/// concrete type without re-resolving the spelling. A non-specialization node is NotFound.
		// ===========================================================================
		QueryResult<const Type*> QueryInstantiation(const QualTypeNode& node, uint32 unitId = 0) const;

		// ===========================================================================
		// LOWERING-TIME SCOPES (doc 06): the ONLY way scopes change while a function body is
		/// lowered. RAII: the guard pops its layer on destruction (including across early returns),
		/// so a leak is structurally impossible. Locals registered in a pushed layer shadow outer
		/// names for every query until the layer pops - variable shadowing, local types, and
		// function-local declarations all work with no extra API. Template bodies are lowered
		/// by pushing a layer holding FAKE ALIASES (T := int32) so `T` resolves to the bound
		// concrete type through the ordinary name lookup - no AST rewriting, ever.
		// ===========================================================================
		ScopeGuard PushScope(Scope::Kind kind, const Core::String& name);
		bool RegisterLocal(const Core::String& name, SymbolInfo info); // current layer
		const SymbolInfo* FindLocal(const Core::String& name) const;   // lowering chain only
		/// Keeps the elements of a pack for the program's lifetime (SymbolInfo::packElements points at them).
		const std::vector<SymbolInfo>* StorePack(std::vector<SymbolInfo> elements) const;

		/// A struct/class/enum declared INSIDE a function body (doc 06): build the concrete type in
		/// the TypeStore and register its name in the CURRENT scope layer, so it is visible exactly
		/// where declared and disappears when the layer pops. Member/field types resolve against the
		/// current scope (so a local struct may name an earlier local or any global). The concrete
		/// type stays owned by the store after the symbol is popped (03's ownership invariant).
		QueryResult<const Type*> RegisterLocalType(const AstNode& decl);

		/// A local `alias Name = T;` declared INSIDE a body: resolve T against the current scope and
		/// register the name as a type symbol in the current layer (like a template's fake alias). No HIR
		/// node is emitted - only later uses of the name produce nodes. Without this, a parseable local
		/// alias fell through StmtLowerer to an internal "statement not yet lowered" failure.
		QueryResult<const Type*> RegisterLocalAlias(const AliasDefinitionNode& alias);

		/// A function declared INSIDE a body: build its signature + lowering record and register the
		/// name in the current scope layer. Signature types resolve against the current scope.
		QueryResult<const FunctionRecord*> RegisterLocalFunction(const FunctionDefinitionNode& fn);

		/// A lambda's closure type and call method (docs/language/07, D-22), built while its function lowers.
		const Closure* CreateClosure(const ClosureSpec& spec);
		/// The closure facts of a lambda's type; null for every other type.
		const Closure* ClosureOf(const Type* t) const;

		// --- Introspection (LSP/DAP/tests + HIR lowering) ------------------------------
		SymbolTable* Symbols() const { return m_Symbols.get(); }
		TypeStore* TypeStorePtr() const { return m_Types.get(); }
		const Core::Vector<const ObjectType*>& ObjectTypes() const;
		const Core::Vector<const EnumType*>& EnumTypes() const;
		const Core::Vector<const UnionType*>& UnionTypes() const { return m_UnionTypes; }
		/// The `asm` blocks written at file scope (docs/language/16 section 2), in the order the program declares them.
		const Core::Vector<const AsmStatementNode*>& FileAssembly() const { return m_FileAssembly; }
		/// A source file's name as diagnostics print it (relative to its folder or library root); empty for an
		/// unknown id.
		Core::String SourceName(uint32 unitId) const { auto it = m_SourceNames.find(unitId); return it != m_SourceNames.end() ? it->second : Core::String(); }
		/// Each unit's absolute path (an in-memory source's name): debug information names the files.
		const std::unordered_map<uint32, std::string>& SourcePaths() const { return m_SourcePaths; }
		/// The prelude's unit (every unit includes it, D-43); 0 when the program has none.
		uint32 PreludeUnit() const { return m_PreludeUnit; }

		/// The processor the program is compiled for (D-62): which intrinsics it may use.
		void SetTarget(const CpuTarget& target) { m_Target = target; }
		const CpuTarget& Target() const { return m_Target; }

		/// The build's projects and the project of each unit of theirs (D-59); library units have none.
		void SetProjects(const ProjectSet* projects, std::unordered_map<uint32, uint32> projectOfUnit)
		{
			m_Projects = projects;
			m_ProjectOfUnit = std::move(projectOfUnit);
		}
		/// Why `symbol`, written in a declaration of the unit being resolved, belongs to a project that unit's project
		/// does not depend on; "" when it may name it (docs/language/15-projects.md, rule 4).
		Core::String OutOfProjectReach(const SymbolInfo& symbol, const Core::String& name) const;

		/// While alive, names a declaration of `unit` resolves are checked against its project (OutOfProjectReach).
		class ProjectReach
		{
		public:
			ProjectReach(const ProgramModel& model, uint32 unit) : m_Model(model), m_Outer(model.m_ReachUnit) { model.m_ReachUnit = unit; }
			~ProjectReach() { m_Model.m_ReachUnit = m_Outer; }
			ProjectReach(const ProjectReach&) = delete;
			ProjectReach& operator=(const ProjectReach&) = delete;
		private:
			const ProgramModel& m_Model;
			uint32 m_Outer;
		};
		/// Every function record in the program (free functions, methods, extension methods), for
		/// phases that lower or emit every body. Order is registration order - stable per build.
		void AllFunctionRecords(Core::Vector<const FunctionRecord*>& out) const;
		/// The bindings a type instantiation was produced with (null when not an instantiation).
		const InstantiationBindings* TypeInstantiationBindings(const Type* instantiated) const;
		/// The bindings an instantiated FREE function template was produced with (null otherwise).
		/// A free function template has no containingType, so its bound params cannot ride on the
		/// method path's TypeInstantiationBindings - without this, a body that names its own param
		/// (e.g. `H<T>` calling `G<Box<T>>`) cannot resolve T and nested instantiation fails.
		const InstantiationBindings* FunctionInstantiationBindings(const FunctionRecord* rec) const;

		/// The program's tests (docs/runtime/03), collected by the builder and read by the test harness.
		const TestRegistry& Tests() const { return m_Tests; }
		TestRegistry& MutableTests() { return m_Tests; }

	private:
		friend class ModelBuilder;
		friend class TemplateInstantiator; // builds concrete types/records from template defs (doc 05)
		friend class TemplateArgumentResolver; // resolves and binds template arguments
		friend class ObjectTemplateBody;  // builds an object-template instance's members
		friend class FunctionTemplates;   // function templates as overload-set members
		friend class TypeResolver;        // resolves type positions against the scope tree (doc 04)
		friend class LoweringScopes;      // RAII lowering-time scopes + function-local decls (doc 06)
		friend class ModelResolver;       // pass-2 resolution reads scopes + resolves types in context
	friend class FreeFunctionResolver; // pass-2 free-function resolver interns signatures + stamps method records
		friend class SymbolQueries;       // name queries (function/operator/variable/member/constant)
		friend class ClosureTypes;        // builds lambda closure types (D-22)
		friend class Specializations;     // selects a template's specialization (D-20)
		friend class ConstEvaluator;      // sizeof(T) resolves T in a template's binding layer
		friend class MemberTemplates;     // function templates declared in a class (BUG-117)
		friend class NestedTypes;         // types declared in a class template's body (BUG-179)
		friend class ReturnLists;         // the anonymous structs of return lists (D-34)
		friend class TemplateMethodDefinitions; // out-of-line definitions of class template methods

		struct TemplateDef
		{
			const TemplateDefinitionNode* params = nullptr;
			const ObjectDefinitionNode* objectBody = nullptr; // object templates
			const QualTypeNode* aliasTarget = nullptr;        // alias templates (isAlias true)
			bool isAlias = false;
			const FunctionDefinitionNode* functionBody = nullptr; // function templates
			const Scope* declScope = nullptr;                // where the template name lives
			const TemplateArgumentListNode* pattern = nullptr; // a specialization's arguments (`Box<T*>`, D-20); null for a primary
			const ObjectType* memberOf = nullptr;            // a member function template's owner (BUG-117)
			Scope* bindingParent = nullptr;                  // the layer its instances' bindings sit on (a class template instance's)
			InstantiationBindings ownerNames;                // an out-of-line definition's own names for the owner's parameters
		};

		QueryResult<const Type*> ResolveTypeInternal(const QualTypeNode& node, uint32 unitId, Scope* bindingScope, const Scope* context) const;
		/// Pass-2 resolution entry (doc 02): resolve a type as written in the defining file's
		/// scope. The builder calls this while the model is mid-resolution, so m_Valid must be set
		/// before pass 2 starts (BeginResolution). bindingScope is null at file scope; context is
		/// the declaring class scope for member types (so unqualified names see members first).
		QueryResult<const Type*> ResolveTypeInContext(const QualTypeNode& node, uint32 unitId, const Scope* context) const;
	public:
		/// Instantiate a type template by LOCAL name within `declScope` (the namespace that declares it).
		/// Public so the lowering side can resolve a TYPE in expression position (`TBox<int32>::MakeVal(5)`);
		/// `outerBinding` is null for a top-level instantiation, `argContext` the scope the arguments are written in.
		QueryResult<const Type*> InstantiateTypeTemplate(const Core::String& name, const Scope* declScope,
			const TemplateArgumentListNode& args, uint32 unitId, Scope* outerBinding, const Scope* argContext) const;
	private:

		/// Visibility-aware qualified walk: "Math::Vec", "S::Inner" (nested types descend into the
		/// class scope), each segment checked against the asking unit's include closure. Returns
		/// nullptr when any segment is missing or not visible. Every name query goes through this.
		const SymbolInfo* FindVisibleQualified(const Core::String& name, uint32 unitId, const Scope* context = nullptr) const;

		std::unique_ptr<TypeStore> m_Types;
		std::unique_ptr<SymbolTable> m_Symbols;
		mutable Core::Vector<std::pair<Core::String, ConstantValue>> m_Constants; // folded: qualified name -> value
		mutable Core::Vector<const ObjectType*> m_ObjectTypes; // mutable: template instantiation registers new types lazily
		mutable Core::Vector<const EnumType*> m_EnumTypes; // mutable: a class template instance's nested enums join lazily
		Core::Vector<const UnionType*> m_UnionTypes;
		Core::Vector<const AsmStatementNode*> m_FileAssembly;
		std::unordered_map<uint32, Core::String> m_SourceNames;
		CpuTarget m_Target;
		uint32 m_PreludeUnit = 0;
		std::unordered_map<uint32, std::string> m_SourcePaths;
		const ProjectSet* m_Projects = nullptr;
		std::unordered_map<uint32, uint32> m_ProjectOfUnit;
		mutable uint32 m_ReachUnit = 0; // the unit whose declaration is being resolved (ProjectReach)
		// name -> defs (one per declaring scope); a deque, because a class template instance registers its nested
		// templates while other definitions are in use
		mutable std::unordered_map<Core::String, std::deque<TemplateDef>> m_TypeTemplates;
		std::unordered_map<Core::String, Core::Vector<TemplateDef>> m_FunctionTemplates; // simple name -> every function template of that name (overloads, namespaces, extensions)
		std::unordered_map<Core::String, Core::Vector<TemplateDef>> m_TypeSpecializations;     // name -> explicit and partial specializations of type templates
		std::unordered_map<Core::String, Core::Vector<TemplateDef>> m_FunctionSpecializations; // name -> explicit specializations of function templates
		mutable std::unordered_map<const FunctionRecord*, const TemplateDef*> m_TemplateOfInstance; // function template instance -> its template (partial ordering)
		// Lazy caches: populated on first use from const query methods, hence mutable.
		mutable Core::Vector<std::pair<Core::String, const Type*>> m_TypeInstantiations;        // cache key -> concrete type
		mutable Core::Vector<std::pair<const Type*, InstantiationBindings>> m_InstantiationBindings; // concrete type -> bindings
		mutable Core::Vector<std::pair<const FunctionRecord*, InstantiationBindings>> m_FuncInstantiationBindings; // instantiated free-function record -> its bound params (a free function template has no containingType, so the method path's TypeInstantiationBindings cannot carry them)
		mutable Core::Vector<std::pair<Core::String, const FunctionRecord*>> m_FunctionInstantiations; // cache key -> record
		mutable std::unordered_map<const ObjectType*, Core::Vector<const FunctionRecord*>> m_ExtensionMethods;
		// Extension methods on a primitive receiver (`extends float`), by receiver type: they are in no method list.
		mutable std::unordered_map<const Type*, Core::Vector<const FunctionRecord*>> m_PrimitiveExtensions;
		// Parallel to each object type's method list: the lowering record for method i (null when
		// the method has no body yet). Method order is stable - it IS the index.
		mutable std::unordered_map<const ObjectType*, Core::Vector<const FunctionRecord*>> m_MethodRecords;
		mutable std::unordered_map<const FunctionType*, const FunctionRecord*> m_RecordBySignature;
		mutable std::map<std::pair<const ObjectType*, LifecycleOp>, const FunctionRecord*> m_SynthesizedLifecycle;
		mutable std::unordered_map<const ObjectType*, const Scope*> m_TypeScopes; // type -> its class scope (member domain)
		mutable std::unordered_map<const ObjectType*, std::deque<TemplateDef>> m_MemberTemplates; // type -> its member function templates (stable addresses)
		mutable std::map<std::vector<const Type*>, const ObjectType*> m_ReturnLists; // element types -> the return list's struct
		// Out-of-line template methods: (class name, method name, parameter count, definition node).
		// An out-of-line definition of a class template's method (`template<T> void Geo::Box<T>::Set(T v)`), registered
		// in pass 1 before any binding exists; an instance takes the body when its in-class declaration has none.
		struct TemplateMethodDef
		{
			Core::String owner; // the template's own name (`Box`)
			Core::String name;
			uint32 arity = 0;
			const FunctionDefinitionNode* definition = nullptr;
			const Scope* scope = nullptr; // where the definition is written
			uint32 unitId = 0;
			mutable const Scope* ownerScope = nullptr; // the scope declaring the template it defines, once known
			mutable bool ownerKnown = false;
		};
		mutable Core::Vector<TemplateMethodDef> m_TemplateMethodDefs;
		TestRegistry m_Tests;
		mutable std::deque<std::vector<SymbolInfo>> m_PackStorage; // the elements of every bound pack (stable addresses)
		std::unordered_map<const ObjectType*, std::unique_ptr<Closure>> m_Closures; // closure type -> its captures and call method
		std::unordered_map<Core::String, uint32> m_ClosuresByOwner;                 // lambdas made so far in each piece of code
		bool m_Valid = false;
	};

} // namespace Scripting
