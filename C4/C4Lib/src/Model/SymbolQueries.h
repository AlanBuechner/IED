#pragma once
#include "Model/ProgramModel.h"

namespace Scripting
{
	// =====================================================================
	// SYMBOL QUERIES (doc 04/08) - the subsystem behind every NAME query in the model. One verb per
	// question, source-level inputs only (a name as written, an operand type, a token), one answer
	// out (a value or an actionable error):
	//   QueryFunction  ONE verb for free functions, methods, operators-by-name, and constructors.
	//                  receiver null = unqualified; non-null = "must exist on this type". Overloads
	//                  resolve HERE against argTypes - the caller gets exactly one signature (or
	//                  Ambiguous/NotFound), never a candidate list. A method's implicit `this` is not
	//                  in argTypes; it is implied by receiver. Function templates instantiate
	//                  transparently and cache like type specializations.
	//   QueryOperator  the token as written ("+", "operator_hash") selects the canonical annotation
	//                  through the ONE operator catalog (doc 08); a null rhs makes it unary.
	//   QueryVariable  ONE verb for local / member / global / static. receiver null + enclosingType
	//                  set answers "member of the enclosing type? global?" in that order.
	//   QueryMember    a member by name on a type (fields and methods together), most-derived first.
	//   QuerySymbol / QueryConstant  raw symbol lookup and folded constants by qualified name.
	// It is friended into ProgramModel so it can reach the symbol table, method records, and
	// extension-method tables; every existing call site keeps working through thin delegates.
	// =====================================================================
	class SymbolQueries
	{
	public:
		QueryResult<const FunctionType*> QueryFunction(const ProgramModel& model, const Core::String& name,
			const Type* receiver = nullptr, const TemplateArgumentListNode* tmplArgs = nullptr,
			const Core::Vector<const Type*>& argTypes = {}, const LookupContext& ctx = {},
			const FunctionRecord** outRecord = nullptr) const;

		QueryResult<OperatorBinding> QueryOperator(const ProgramModel& model, const Core::String& opToken,
			const Type* lhs, const Type* rhs, const LookupContext& ctx = {}) const;

		QueryResult<VariableInfo> QueryVariable(const ProgramModel& model, const Core::String& name,
			const Type* receiver = nullptr, const LookupContext& ctx = {}, const Scope* scopeContext = nullptr) const;

		QueryResult<MemberBinding> QueryMember(const ProgramModel& model, const Type* base, const Core::String& name) const;

		QueryResult<const SymbolInfo*> QuerySymbol(const ProgramModel& model, const Core::String& name, uint32 unitId = 0,
			const Scope* context = nullptr) const;

		/// A constant by name, resolved from `context` outward (the root when null).
		QueryResult<ConstantValue> QueryConstant(const ProgramModel& model, const Core::String& name, uint32 unitId = 0,
			const Scope* context = nullptr) const;

	private:
		friend class ProgramModel; // the only caller (public verbs delegate here)

		/// Pick one signature from a candidate set against the call's argument types (doc 08):
		/// Resolved -> its signature; Ambiguous/NotFound -> an actionable error.
		static Core::String BracedArgumentHint(const Core::Vector<const Type*>& argTypes);
		/// "no member 'x' in 'T'", with the member it most likely meant (MemberSuggestion).
		static Core::String NoMember(const ProgramModel& model, const ObjectType* type, const Core::String& name);
		QueryResult<const FunctionType*> ResolveAndReturn(const ProgramModel& model, const Core::Vector<OverloadCandidate>& candidates,
			const Core::Vector<const Type*>& argTypes, const Core::String& name, const FunctionRecord** outRecord = nullptr) const;
		/// A method called on a receiver that is no object (a number, a void*): its type's extension methods of the name,
		/// or an error naming the type (BUG-423).
		QueryResult<const FunctionType*> QueryNonObjectReceiver(const ProgramModel& model, const Core::String& name, const Type* receiverType,
			const Core::Vector<const Type*>& argTypes, const LookupContext& ctx, const FunctionRecord** outRecord) const;

		static int FunctionTemplateOrder(const ProgramModel& model, const FunctionRecord* a, const FunctionRecord* b);
		/// Does a record carry the given operator annotation (the [operator("...")] set, doc 08)?
		static bool ContainsAnnotation(const FunctionRecord* rec, const Core::String& annotation);

		/// The methods named `name` (or carrying `annotation`) on `type` and every base sub-object, own first.
		void CollectMethods(const ProgramModel& model, const ObjectType* type, const Core::String& name,
			bool isOperatorName, const Core::String& annotation, Core::Vector<OverloadCandidate>& out) const;
		/// Keep the methods a receiver with `access` can call (D-31); an error when it can call none of them.
		QueryResult<void> SelectByReceiverAccess(Core::Vector<OverloadCandidate>& candidates, ReceiverAccess access, const Core::String& name) const;
		/// An error when two candidates share a signature but come from unrelated bases of `type`.
		QueryResult<void> InheritedSignatureClash(const ObjectType* type, const Core::Vector<OverloadCandidate>& candidates,
			const Core::String& name) const;
		/// `Derived::F` naming a static method of a base (BUG-417): the nearest base's static overloads of F.
		static void CollectInheritedStatics(const ProgramModel& model, const Core::String& name, uint32 unitId, const Scope* scope,
			Core::Vector<OverloadCandidate>& candidates);
	};
} // namespace Scripting
