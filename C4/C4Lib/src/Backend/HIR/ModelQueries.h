#pragma once
#include "Model/ProgramModel.h"
#include "Backend/HIR/HIRNodes.h" // IntrinsicOp (the token -> op maps return these)

namespace Scripting
{
	// =====================================================================
	// MODEL QUERIES (HIR) - the ONE model-facing surface for lowering. Every node lowerer talks
	// to the ProgramModel through this small helper, so there is exactly one place that knows who
	// is asking (unitId + enclosingType), one error channel, and one set of query wrappers. This is
	// what keeps ExprLowerer / StmtLowerer honest: they emit HIR and ask questions; they never
	// touch the model directly. It also owns the token -> built-in-op maps for primitives, so the
	// two phases cannot disagree about what a token means (mirrors the model's OperatorTable).
	// =====================================================================
	class ModelQueries
	{
	public:
		explicit ModelQueries(ProgramModel* model) : m_Model(model) {}

		// --- the asking context (who is resolving) --------------------------------------
		LookupContext Ctx(uint32 unitId, const ObjectType* enclosingType, const Scope* scope = nullptr) const
		{
			LookupContext c; c.unitId = unitId; c.enclosingType = enclosingType; c.scope = scope; return c;
		}

		// --- type / variable / function / operator / member (doc 04 + doc 08 verbs) -------
		QueryResult<const Type*> ResolveTypeCtx(const QualTypeNode& n, uint32 unitId, const Scope* context) const
		{
			return m_Model->ResolveTypeInUnitContext(n, unitId, context);
		}
		QueryResult<VariableInfo> QueryVariable(const Core::String& name, const Type* receiver,
			uint32 unitId, const ObjectType* enclosingType, const Scope* scopeContext = nullptr) const
		{
			return m_Model->QueryVariable(name, receiver, Ctx(unitId, enclosingType, scopeContext));
		}
		/// `access` is how the receiver may be used: a read-only one calls only const methods (D-31).
		const TemplateNameType* QueryTemplateName(const Core::String& name, uint32 unitId, const Scope* scope) const
		{
			return m_Model->QueryTemplateName(name, unitId, scope);
		}
		QueryResult<const FunctionType*> QueryFunction(const Core::String& name, const Type* receiver,
			const TemplateArgumentListNode* tmplArgs, const Core::Vector<const Type*>& argTypes,
			uint32 unitId, const ObjectType* enclosingType, const Scope* scopeContext = nullptr, const FunctionRecord** outRecord = nullptr,
			ReceiverAccess access = ReceiverAccess::Writable) const
		{
			LookupContext ctx = Ctx(unitId, enclosingType, scopeContext);
			ctx.receiver = access;
			return m_Model->QueryFunction(name, receiver, tmplArgs, argTypes, ctx, outRecord);
		}
		/// `left` / `right`: how each operand may be used when the operator is its method (D-31).
		QueryResult<OperatorBinding> QueryOperator(const Core::String& token, const Type* lhs, const Type* rhs,
			uint32 unitId, const ObjectType* enclosingType, ReceiverAccess left = ReceiverAccess::Writable,
			ReceiverAccess right = ReceiverAccess::Writable) const
		{
			LookupContext ctx = Ctx(unitId, enclosingType);
			ctx.receiver = left;
			ctx.operand = right;
			return m_Model->QueryOperator(token, lhs, rhs, ctx);
		}
		QueryResult<MemberBinding> QueryMember(const Type* base, const Core::String& name) const
		{
			return m_Model->QueryMember(base, name);
		}
		QueryResult<ConstantValue> QueryConstant(const Core::String& name, uint32 unitId, const Scope* context) const
		{
			return m_Model->QueryConstant(name, unitId, context);
		}
		QueryResult<const Type*> QueryType(const Core::String& name, uint32 unitId, const Scope* scopeContext = nullptr) const
		{
			return m_Model->QueryType(name, unitId, scopeContext);
		}
		/// `name<args>` named in expression position (`Vector<int32>(4)`), looked up from `scopeContext` outward.
		/// The instance of the type template `name` (plain, or qualified by namespaces: `Ns::Box`) for `args`.
		QueryResult<const Type*> QueryTemplateInstance(const Core::String& name, const TemplateArgumentListNode& args,
			uint32 unitId, const Scope* scopeContext) const;

		/// Resolve an ALREADY-PARSED qualified type name (a NamespaceQualifiedNameNode) to a concrete
		/// type, walking namespace segments and instantiating a final-segment template specialization.
		/// This is the lowering-side twin of TypeResolver's qualified walk: it is used when a TYPE
		/// appears in EXPRESSION position as the base of a static-member call (`TBox<int32>::MakeVal(5)`),
		/// where the parser gives us an NQN node, not a QualTypeNode. Returns nullptr if any segment is
		/// missing, a namespace that is not a type path, or the final name is not a (template) type.
		const Type* ResolveNqnType(const NamespaceQualifiedNameNode* nqn, uint32 unitId, const Scope* scopeContext) const;
		/// The same, with why a name is no type (a template instance that cannot be made says so).
		QueryResult<const Type*> QueryNqnType(const NamespaceQualifiedNameNode* nqn, uint32 unitId, const Scope* scopeContext) const;
		QueryResult<const Type*> SegmentType(const NamespaceQualifiedNameNode::Segment& seg, const Scope* from, uint32 unitId,
			const Scope* scopeContext) const;

		/// The model itself (for the few subsystems that need a direct handle, e.g. ConstEvaluator).
		ProgramModel* Model() const { return m_Model; }

		// --- lowering records (what emission needs, from a resolved signature or method) ----
		const FunctionRecord* RecordFor(const FunctionType* sig) const { return m_Model->RecordFor(sig); }
		const FunctionRecord* RecordForMethod(const ObjectType* t, const ObjectType::MethodInfo* m) const
		{
			return m_Model->RecordForMethod(t, m);
		}

		// The method on `t` (or a base, most-derived first) carrying the given [operator(...)] annotation
		// - e.g. "create" for new T(args), "delete" for the destroy path. Null when none is declared. This is
		// how allocation/deallocation find their constructor and destructor without re-resolving by name. When a
		// type declares SEVERAL overloads of the same operator (e.g. `Construct()` and `Construct(int32)`), the
		// one whose parameter count matches `argc` is chosen; with no match the first is returned (the caller
		// then reports an arity error rather than silently picking a wrong overload).
		const FunctionRecord* FindOperatorMethod(const ObjectType* t, const Core::String& annotation, uint32 argc = 0xFFFFFFFF) const;

		// --- element / builtin types (codegen-facing facts the lowerer needs) --------------
		TypeStore* TypeStore() const { return m_Model->TypeStorePtr(); }
		const Type* ElementOf(const Type* t) const { return m_Model->ElementOf(t); }

		/// The [operator("cast")] extension method that converts a value OF `source` type to a `target`
		/// type, or null when none is declared. Handles BOTH receiver kinds: an object receiver (a cast
		/// method on the struct, e.g. `(float)a` where ToFloat() extends OpVec2) and a PRIMITIVE receiver
		/// (`(OpIVec2)5.5f` where ToIVec2() extends float - registered as a free function with
		/// primitiveReceiverType, not in any type's method list). The match is on the operand's SOURCE type
		/// (the cast's `this`) and the method's RETURN type (the cast target).
		const FunctionRecord* FindCastMethod(const Type* source, const Type* target) const;

		// --- lowering-time scope: the ONLY way local names enter/leave lookup (doc 06) ------
		ScopeGuard PushScope(Scope::Kind kind, const Core::String& name) { return m_Model->PushScope(kind, name); }
		bool RegisterLocal(const Core::String& name, SymbolInfo info) { return m_Model->RegisterLocal(name, info); }
		const SymbolInfo* FindLocal(const Core::String& name) const { return m_Model->FindLocal(name); }

		// A type (struct/class/enum) or function declared INSIDE a body: concrete in the store,
		// its name alive only in the current scope layer (doc 06). The Type*/record outlive the pop.
		QueryResult<const Type*> RegisterLocalType(const AstNode& decl) { return m_Model->RegisterLocalType(decl); }
		QueryResult<const FunctionRecord*> RegisterLocalFunction(const FunctionDefinitionNode& fn) { return m_Model->RegisterLocalFunction(fn); }
		QueryResult<const Type*> RegisterLocalAlias(const AliasDefinitionNode& alias) { return m_Model->RegisterLocalAlias(alias); }

		// --- token -> built-in op for PRIMITIVES (null = not a primitive op; try a method) --
		static const IntrinsicOp* IntrinsicForBinary(const Core::String& tok);
		static const IntrinsicOp* IntrinsicForUnary(const Core::String& tok);

	private:
		ProgramModel* m_Model;   // read-only except the scope API
	};
} // namespace Scripting
