#pragma once
#include <vector>
#include "Model/Types.h"
#include "Model/ConstantValue.h"
#include "Model/ModelWrites.h"
#include "Compiler/ErrorTracker.h"
#include "Core/Core.h"
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace Scripting
{
	class Scope; // scope tree layer (defined below); FunctionRecord references it by pointer

	/// What a name denotes. A function name denotes an OVERLOAD SET (doc 08): one record per
	/// signature, resolved to one at the call site where argument types are known.
	enum class SymbolKind { Type, Function, Variable, Namespace };

	/// A function the compiler writes for an object type (docs/language/06): its default construction, its
	/// destruction or its memberwise copy. None for every function written in source.
	enum class LifecycleOp : uint8 { None, Create, Delete, Copy };

	/// Where a record came from: written as it is, a member of a class-template instance, or an instance of a
	/// function template (docs/language/08). Template-made bodies are lowered only when used (D-16).
	enum class TemplateOrigin : uint8 { None, ClassMember, FunctionTemplate };

	/// Everything lowering needs to start a function's scope: its signature, the scope it lives
	/// in (what PushFunctionScope pushes under), and its implicit-this type when it is a method.
	struct FunctionRecord
	{
		const FunctionType* type = nullptr;
		const Scope* scope = nullptr;               // enclosing scope (class / namespace / file)
		const ObjectType* containingType = nullptr; // implicit this; null for free + static
		const Type* primitiveReceiverType = nullptr; // implicit this when the receiver is a PRIMITIVE (extension method `extends float`); containingType stays null then
		bool isStatic = false;
		bool isVirtual = false;
		bool isExtensionMethod = false;
		bool isLocal = false;                        // a function declared INSIDE a body: emitted as part of its parent's tree, never top-level
		bool isBuiltin = false;                      // declared by the prelude (D-43): the runtime's function, not the program's
		bool isLinked = false;                       // [link("sym")]: bound to an external symbol - the definition lives OUTSIDE C4 (C/C++ interop), so it is callable even with no C4 bodyNode
		bool isHostDefined = false;                  // a script compiling against its host (docs/runtime/04): no body here, but the host has the code
		bool isNoexcept = false;                     // `noexcept`: an exception leaving it terminates the program
		bool isConst = false;                        // a const method: `this` is read-only, and a const object can call it (D-31)
		AccessModifier access = AccessModifier::Public; // a method's visibility (D-28); free functions are public
		LifecycleOp synthesized = LifecycleOp::None; // a compiler-written body (no bodyNode): HIR's Lifecycle lowers it
		TemplateOrigin templateOrigin = TemplateOrigin::None;
		mutable const AstNode* bodyNode = nullptr;  // the CodeBlockNode, or nullptr (declaration only)
		mutable Core::Vector<const AstNode*> declarations; // every FunctionDefinitionNode that declares or defines it, in order
		mutable uint32 definingUnit = 0;            // source id of the defining file (0 = builtin); mutable: a body attached at pass 2 re-points it to the definition's unit
		mutable uint32 declUnit = 0;                // the unit where this record was DECLARED - stable, used for VISIBILITY. Never re-pointed at body-attach (that would hide the name from header-only includers).
		mutable Core::Vector<uint32> declUnits;     // every unit that declares it (BUG-340): each makes the name visible to its includers
		Core::String mangledName;                   // unique emitted symbol for this signature
		Core::String name;                          // plain C4-side name (not mangled) - reflection display + group queries
		Core::Vector<Core::String> groups;          // [group("name")] annotations on the declaration (reflection, plan 14)
		uint32 minArity = 0;                        // params minus trailing defaults (doc 08 step 1)
		// Parameter NAMES, in source order. The interned FunctionType drops them (names are not type
		// identity), but the lowerer needs them to bind each param slot as a local - so they live on
		// the record, which is per-declaration. Parallel to `type->GetParameters()`. Mutable: when an
		// out-of-line definition attaches at pass 2 its parameter names (which C++ allows to differ from
		// the declaration's) replace these, so the body - which references the DEFINITION's names - lowers.
		mutable Core::Vector<Core::String> paramNames;
		// DEFAULT-ARGUMENT EXPRESSIONS, in source order (parallel to type->GetParameters()); null where a
		// parameter has no default. The FunctionType drops them too, but the CALL LOWERER needs them to fill
		// trailing arguments the caller omitted (`F(1)` for `F(int32 a, int32 b = 5)` lowers the `5` here).
		// Stored as raw AST pointers (the parse tree outlives the record) and re-lowered in the CALLER's
		// context at each call site - C++ evaluates defaults per call.
		Core::Vector<const AstNode*> paramDefaults;
		// Parameters declared const (`const int32 x`, D-30), parallel to paramNames: read-only in the body. Taken
		// from the definition, like the names.
		mutable std::vector<bool> paramIsConst;
		bool ParamIsConst(uint32 i) const { return i < paramIsConst.size() && paramIsConst[i]; }
		// The function parameter packs of a template instance (`Ts... args`, D-19): the parameters from `first`
		// on, `count` of them (possibly none), are its elements.
		struct ParameterPack { Core::String name; uint32 first = 0; uint32 count = 0; };
		Core::Vector<ParameterPack> parameterPacks;
		// Operator annotations (doc 08): a method may carry SEVERAL - `[operator("create","copy")]`
		// or chained `[operator("add")] [operator("sub")]`. Both `QueryOperator` (annotation ->
		// candidates) and `QueryFunction` ("operator_add" name -> annotation "add") read this set.
		Core::Vector<Core::String> operatorAnnotations;
	};

	/// True when C4 emits a body for the record: written in source, or written by the compiler.
	inline bool HasBody(const FunctionRecord& rec) { return rec.bodyNode || rec.synthesized != LifecycleOp::None; }

	/// True when a call to the record reaches code: a body here, a foreign function, or the host's (in a script).
	inline bool HasCode(const FunctionRecord& rec) { return HasBody(rec) || rec.isLinked || rec.isHostDefined; }

	/// One declaration of a name in one scope layer. A name can have SEVERAL entries in the same
	/// layer only when they are `static` (internal linkage: each file's is its own symbol).
	/// Functions carry their whole overload set; a set with one member behaves exactly like the
	/// old single-symbol case - nothing downstream branches on "is this overloaded".
	struct SymbolInfo
	{
		SymbolKind kind = SymbolKind::Type;
		const Type* type = nullptr;                          // Type and Variable kinds
		const Scope* scope = nullptr;                        // Namespace kind: the scope this name opens
		Core::Vector<const FunctionRecord*> functions;       // Function kind (overload set)
		bool isConst = false;                                // Variable kind: compile-time constant
		bool isStatic = false;                               // Variable/Function: internal linkage / static member
		uint32 definingUnit = 0;                             // visibility domain (0 = builtin, sees all)
		const AstNode* declNode = nullptr;                   // the declaration node (LSP/DAP)
		const AstNode* initializer = nullptr;                // Variable kind: the initializer expression (folding input)
		ConstantValue constValue;                            // isConst: the folded value (also non-type template params)
		const void* templateDef = nullptr;                   // set when this name declares a template (ProgramModel::TemplateDef*)
		// The emitted symbol for a Variable (global / static member): its simple name prefixed by every
		// enclosing namespace AND class (walking up to file scope), so `Ns::x` and a namespaced class's
		// `A::s` are unique and stable across declaration/definition. Functions use FunctionRecord::mangledName;
		// this is the variable counterpart codegen uses to name its GlobalVariable. Empty for non-variables.
		Core::String mangledName;
		// A [link("sym")] annotation on a variable: alias this exact foreign symbol instead of defining
		// our own storage (doc 04 interop). When set, codegen emits an EXTERNAL declaration named sym and
		// never allocates local storage for it. Empty = define our own global as usual.
		Core::String linkSymbol;
		// Plain C4-side name of this variable (not the mangled symbol) - reflection display + group
		// queries (plan 14). Set at registration alongside mangledName.
		Core::String plainName;
		// [group("name")] annotations on the declaration (reflection, plan 14). Empty = no group.
		Core::Vector<Core::String> groups;
		// A lowering-time local (a parameter or a body local, registered in a Function/Block layer): its HIR
		// slot, and whether the slot holds the referent's address (reference parameters and locals, which
		// read through it). Living on the scoped symbol is what makes shadowing pick the right slot.
		static constexpr uint32 kNoSlot = 0xFFFFFFFFu;
		uint32 localSlot = kNoSlot;
		bool localIsReference = false;
		bool localRefersToConst = false;
		// A lambda capture (docs/language/07): the index of the closure field that holds it (localIsReference:
		// the field holds the variable's address). kNoSlot otherwise.
		uint32 localCaptureField = kNoSlot;
		// A pack (D-19): a template parameter pack bound by an instantiation (`Ts` := int32, float) or a function
		// parameter pack (`args`). Each element is what the name means in element i of an expansion. Owned by
		// the ProgramModel.
		const std::vector<SymbolInfo>* packElements = nullptr;
		// A variable's storage modifiers (D-29): every load and store of it is volatile; each thread has its own copy.
		bool isVolatile = false;
		bool isThreadLocal = false;
		// The scope this symbol is registered in: a constant's initializer is folded there.
		const Scope* declaringScope = nullptr;
		bool IsPack() const { return packElements != nullptr; }
		/// A variable stored in a function's frame (a slot or a capture), as opposed to a type, a constant or a
		/// function bound in a lowering layer.
		bool IsFrameVariable() const { return kind == SymbolKind::Variable && (localSlot != kNoSlot || localCaptureField != kNoSlot); }
		/// A const global or static whose value is known at compile time - a number, or a const pointer to a string
		/// literal (`const char const* kDigits = "..."`): reads use the value, and its storage is read-only (BUG-190), so
		/// a loop's loads of it fold away even beside stores through char pointers.
		bool IsFoldedConstant() const
		{
			const ConstantValue::Kind k = constValue.kind;
			return isConst && !isThreadLocal && (k == ConstantValue::Kind::Int64 || k == ConstantValue::Kind::Float64 || k == ConstantValue::Kind::Bool ||
				k == ConstantValue::Kind::String);
		}
	};

	/// A layer in the scope tree: file -> namespace -> class -> function -> block. Lookup walks
	/// this layer then the parent - innermost wins (the usual shadowing rule). Layers are owned
	/// by their parent; ScopeGuard pops them by lifetime, so a leak is structurally impossible.
	struct Scope
	{
		enum class Kind { File, Namespace, Class, Function, Block };

		Scope(Kind kind, const Core::String& name) : m_Kind(kind), m_Name(name) {}

		Kind GetKind() const { return m_Kind; }
		/// A file or a namespace: where a `static` free function is the file's own (internal linkage, D4), the
		/// namespace being shared by every file that opens it (BUG-289).
		bool HoldsFileStatics() const { return m_Kind == Kind::File || m_Kind == Kind::Namespace; }
		const Core::String& GetName() const { return m_Name; }
		Scope* GetParent() const { return m_Parent; }

		/// Register a declaration in THIS layer. Function kind appends to the overload set (doc 08).
		/// A same-name non-function redeclaration is allowed only when both are static (internal
		/// linkage keeps them distinct symbols); the model builder reports true conflicts as errors.
		bool Register(const Core::String& name, const SymbolInfo& info, const FunctionRecord* fn = nullptr);

		/// Re-point a function name's VISIBILITY unit to `unit`. A declaration makes the symbol visible to
		/// every unit whose include closure contains its own file; the entry was created (possibly by an
		/// earlier definition in another file) with that file's unit, and includers of THIS declaration file
		/// must see it. No-op when the name is absent or not a function. The FunctionRecord's definingUnit
		/// (the body-lowering context) is separate and untouched.
		void SetFunctionVisibilityUnit(const Core::String& name, uint32 unit);

		/// All entries for a name in THIS layer only (no parent walk). Empty when absent.
		const Core::Vector<std::unique_ptr<SymbolInfo>>& Entries(const Core::String& name) const;

		/// Innermost-first lookup: this layer, then parent, up to the root. Returns nullptr when
		/// the name is not declared anywhere on the chain.
		const SymbolInfo* Find(const Core::String& name) const;

		/// Every symbol in THIS layer (no parent walk), for whole-tree passes (const folding).
		void Enumerate(Core::Vector<std::pair<Core::String, SymbolInfo*>>& out) const;

		const Core::Vector<std::unique_ptr<Scope>>& GetChildren() const { return m_Children; }

		/// Visit every symbol registered in THIS layer only (no parent walk). Used to enumerate the whole
		/// tree by recursing through children.
		void ForEachSymbol(const std::function<void(const SymbolInfo&)>& visit) const
		{
			if (!m_Symbols) return;
			for (const auto& [name, infos] : *m_Symbols)
				for (const auto& info : infos)
					visit(*info);
		}
		std::unique_ptr<Scope> TakeChild(Scope* child);

		// Class kind only: the type this scope is the member domain of (set by the model builder).
		const ObjectType* objectType = nullptr;

	private:
		friend class SymbolTable;
		Kind m_Kind;
		Core::String m_Name;
		Scope* m_Parent = nullptr;
		// Asked before a name joins this layer: a permanent layer (the root, a namespace, a class, a template's
		// binding) has the model's; a lowering's own layer, which no other lowering sees, has none.
		const ModelWrites* m_Writes = nullptr;
		static constexpr size_t kComparedNames = 4; // Entries compares names in a layer of at most this many
		using Symbols = std::unordered_map<Core::String, Core::Vector<std::unique_ptr<SymbolInfo>>>;
		// Made by the first Register: most of lowering's block layers declare nothing, and an empty map allocates.
		std::unique_ptr<Symbols> m_Symbols;
		Core::Vector<std::unique_ptr<Scope>> m_Children;
	};

	/// The namespaces and classes enclosing `scope` (itself included), outermost first.
	inline Core::Vector<Core::String> EnclosingNames(const Scope* scope)
	{
		Core::Vector<Core::String> innerFirst;
		for (const Scope* s = scope; s; s = s->GetParent())
			if ((s->GetKind() == Scope::Kind::Namespace || s->GetKind() == Scope::Kind::Class) && !s->GetName().empty())
				innerFirst.Push(s->GetName());
		Core::Vector<Core::String> names;
		for (uint32 i = innerFirst.Count(); i-- > 0; ) names.Push(innerFirst[i]);
		return names;
	}

	/// The canonical emitted symbol name for a file-scope or member declaration registered in `scope`: its
	/// simple name prefixed by every enclosing namespace and class (walking up to the file scope). A name
	/// written unqualified inside a namespace/class and one written qualified resolve to the SAME string, so
	/// a declaration + definition always deduplicate to one symbol. Shared by functions and variables so the
	/// whole backend has ONE naming rule.
	/// Whether `record` is the program's entry point `entryName` (`main`): a free function of that name at file
	/// scope, whatever overload suffix its symbol carries.
	inline bool IsEntryPoint(const FunctionRecord& record, const Core::String& entryName)
	{
		return !record.containingType && !record.isStatic && !record.primitiveReceiverType && record.name == entryName &&
			(!record.scope || record.scope->GetKind() == Scope::Kind::File || !record.scope->GetParent());
	}

	inline Core::String CanonicalSymbolName(const Scope* scope, const Core::String& simpleName)
	{
		Core::Vector<Core::String> parts;
		parts.Push(simpleName);
		for (const Scope* s = scope; s && s->GetKind() != Scope::Kind::File; s = s->GetParent())
			if (!s->GetName().empty())
				parts.Push(s->GetName());
		Core::String name;
		for (int i = (int)parts.Count() - 1; i >= 0; --i)
		{
			if (!name.empty()) name += "_";
			name += parts[i];
		}
		return name;
	}

	/// The symbol of a function named `name` declared in `scope` (its namespaces, or a type's scope and those
	/// around it) taking `signature`'s parameters: `Ns_Name_<parameters>` (FunctionType::OverloadSuffix).
	Core::String FunctionSymbol(const Scope* scope, const Core::String& name, const FunctionType& signature);
	/// A method's symbol: FunctionSymbol in its type's scope (after the bare type name when the type has none),
	/// with `_const` for a const method, so `Get()` and `Get() const` are two functions.
	Core::String MethodSymbol(const Scope* typeScope, const ObjectType& owner, const Core::String& name,
		const FunctionType& signature, bool isConst);

	class SymbolTable;

	/// One lowering's place in the scope tree (doc 06): the innermost layer it pushed. Each lowering has its own, so
	/// lowerings on several threads never see each other's locals. While it lives it is its thread's lowering position
	/// (SymbolTable::Current); a lowering inside another on the same thread (a lambda's body, a local function's)
	/// starts where the outer one is. Its layers hang under that place without joining the tree's children, and the
	/// table keeps them when it ends: a local function's record names the layer it was declared in.
	class ScopeCursor
	{
	public:
		explicit ScopeCursor(SymbolTable& table);
		~ScopeCursor();
		ScopeCursor(const ScopeCursor&) = delete;
		ScopeCursor& operator=(const ScopeCursor&) = delete;

		/// The elements of a pack this lowering binds (`args` of `Ts... args`), kept as long as its layers.
		const std::vector<SymbolInfo>* KeepPack(std::vector<SymbolInfo> elements);

	private:
		friend class SymbolTable;
		friend class ScopeGuard;
		SymbolTable& m_Table;
		ScopeCursor* m_Outer;   // the thread's cursor before this one
		Scope* m_Position;
		Core::Vector<std::unique_ptr<Scope>> m_Layers;
		std::unique_ptr<std::deque<std::vector<SymbolInfo>>> m_Packs = std::make_unique<std::deque<std::vector<SymbolInfo>>>();
	};

	/// RAII scope handle (doc 06): pops its layer on destruction, including across early returns
	/// and error bails. This is the only way lowering-time scopes change.
	class ScopeGuard
	{
	public:
		ScopeGuard() = default;
		~ScopeGuard();
		ScopeGuard(const ScopeGuard&) = delete;
		ScopeGuard& operator=(const ScopeGuard&) = delete;

		Scope* GetScope() const { return m_Scope; }
		bool Valid() const { return m_Scope != nullptr; }

	private:
		friend class SymbolTable;
		SymbolTable* m_Table = nullptr;
		ScopeCursor* m_Cursor = nullptr; // the lowering whose layer this is; none for a layer on the table's own position
		Scope* m_Scope = nullptr;
		ScopeGuard(SymbolTable* table, ScopeCursor* cursor, Scope* scope);
	};

	/// Scoped name resolution for one program (doc 03/06). Owns the scope tree and every
	/// FunctionRecord. Visibility is enforced at lookup time from the asking unit: a symbol is
	/// visible iff its defining file is in that unit's include closure (or it is a builtin,
	/// definingUnit 0). Callers never write a visibility check - FindVisible does it.
	class SymbolTable
	{
	public:
		/// `writes` is asked before a permanent layer, a name in one, or a record is added (a frozen model adds none).
		explicit SymbolTable(const ModelWrites* writes = nullptr);

		/// The program root scope; every namespace and file-scope declaration hangs under it.
		Scope* GetRoot() const { return m_Root.get(); }

		// --- Enumeration (codegen / tooling) ----------------------------------------------
		/// Visit only top-level variables - those in File/Namespace/Class layers (globals + static data
		/// members), which are the ONLY ones with backing storage a consumer can take the address of.
		/// Function/Block-layer locals are skipped (they have no global symbol). This is what reflection
		/// registration (plan 14) needs: it emits `extern` + address-taking for each, so a local would break.
		void CollectGlobalVariables(const std::function<void(const SymbolInfo&)>& visit) const;
		/// The same, with the scope each variable is declared in (a namespace, a class or the file).
		void ForEachGlobalVariable(const std::function<void(const SymbolInfo&, const Scope&)>& visit) const;

		// --- Lowering-time scope management (doc 06) -------------------------------------
		/// Push a new layer under the current lowering position: the calling thread's ScopeCursor's, or the
		/// table's own when it has none. The caller keeps the returned guard alive for exactly as long as the layer
		/// must exist; its end makes the layer's parent current again.
		ScopeGuard PushScope(Scope::Kind kind, const Core::String& name);

		/// Create a PERMANENT layer under `parent` (builder registration: class and namespace
		/// scopes live for the model's lifetime and are not RAII). Not the lowering position. A block made while a
		/// lowering runs is that lowering's (ScopeCursor): alive as long, outside the tree's children.
		Scope* CreateScope(Scope::Kind kind, const Core::String& name, Scope* parent);

		/// The current lowering position: the calling thread's ScopeCursor's, else the innermost layer pushed on the
		/// table's own position, or the root when none.
		Scope* Current() const;

		// --- Records ----------------------------------------------------------------------
		/// Own a FunctionRecord for a symbol. Stable for the model's lifetime. A C4 function's symbol that
		/// another function already has gets `_<n>`, so every function has its own (BUG-153); a builtin or
		/// [link] symbol names a foreign function and is kept.
		const FunctionRecord* AddFunctionRecord(FunctionRecord record);
		/// A script compile's host table: a record with no body whose symbol it lists is the host's (isHostDefined).
		void SetHostSymbols(const std::unordered_set<std::string>* symbols) { m_HostSymbols = symbols; }

		/// Every owned record, in registration order (stable per build). Read-only view.
		const Core::Vector<std::unique_ptr<FunctionRecord>>& Records() const { return m_Records; }

		// --- Visibility (doc 09) -----------------------------------------------------------
		/// Record unitId's include closure (the units whose symbols are visible from it).
		void SetIncludeClosure(uint32 unitId, const Core::Vector<uint32>& closure);
		bool IsVisible(uint32 definingUnit, uint32 askingUnit) const;
		/// Whether any declaration of `function` is visible from `askingUnit`.
		bool DeclarationVisible(const FunctionRecord& function, uint32 askingUnit) const;

		/// Innermost-first lookup that also enforces include-closure visibility. `askingUnit`
		/// 0 (builtin context) sees everything. Among same-layer entries, the asking unit's own
		/// declaration wins (statics), then any visible one.
		const SymbolInfo* FindVisible(const Scope* from, const Core::String& name, uint32 askingUnit) const;

		/// Every symbol of every scope, with the scope that declares it (whole-program passes: constant folding).
		void ForEachScopedSymbol(const std::function<void(const Core::String&, SymbolInfo*, const Scope&)>& visit) const;

	private:
		friend class ScopeGuard;  // pops m_Current on destruction
		friend class ScopeCursor; // hands its layers over when it ends
		ScopeCursor* ActiveCursor() const;
		void Adopt(ScopeCursor& cursor);

		const ModelWrites* m_Writes = nullptr;
		std::unique_ptr<Scope> m_Root;
		Core::Vector<std::unique_ptr<Scope>> m_Retired; // popped lowering layers, kept so pointers into them stay valid
		Core::Vector<std::unique_ptr<std::deque<std::vector<SymbolInfo>>>> m_RetiredPacks; // the packs their lowerings bound
		std::mutex m_Adopting; // lowerings on several threads end at once
		Scope* m_Current = nullptr;
		Core::Vector<std::unique_ptr<FunctionRecord>> m_Records;
		std::unordered_set<Core::String> m_ClaimedSymbols; // the symbols of the C4 functions recorded so far
		const std::unordered_set<std::string>* m_HostSymbols = nullptr; // a script compile's host table (docs/runtime/04)
		std::unordered_map<uint32, std::unordered_set<uint32>> m_Closures;
	};

} // namespace Scripting
