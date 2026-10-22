#include "Model/SymbolQueries.h"
#include "Model/OverloadResolution.h"
#include "Model/ConstEvaluator.h"
#include "Model/OperatorTable.h"
#include "Model/Templates/PartialOrdering.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Model/TypeHandle.h" // UnwrapToValue brick (receiver unwraps to the value type)
#include "Model/Types/InitListType.h" // built-in aggregate members (data/size)
#include "Model/Types/ClassHierarchy.h" // every base sub-object, not only the first
#include "Model/Templates/FunctionTemplates.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Compiler/ErrorTracker.h"

namespace Scripting
{
	namespace
	{
		void CollectHiddenPrelude(const ProgramModel& model, const Core::String& name, Core::Vector<OverloadCandidate>& candidates)
		{
			const SymbolInfo* hidden = model.HiddenPrelude() ? model.HiddenPrelude()->Find(name) : nullptr;
			if (!hidden || hidden->kind != SymbolKind::Function) return;
			for (const FunctionRecord* rec : hidden->functions) candidates.Push({ rec->type, rec, rec->minArity });
		}
	}

	namespace
	{
		const Type* StripAliases(const Type* t)
		{
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t;
		}

		// Two method signatures declare the same function when their parameter lists match (method types are
		// built per declaration, so their FunctionType pointers differ).
		bool SameParameters(const FunctionType* x, const FunctionType* y)
		{
			if (x == y) return true;
			if (!x || !y || x->GetParameterCount() != y->GetParameterCount() || x->IsVariadic() != y->IsVariadic()) return false;
			for (uint32 i = 0; i < x->GetParameterCount(); ++i)
				if (StripAliases(x->GetParameters()[i].m_Type) != StripAliases(y->GetParameters()[i].m_Type)) return false;
			return true;
		}
	}

	// ---------------------------------------------------------------------------
	namespace
	{
		bool IsInstanceMethod(const FunctionRecord* rec)
		{
			return rec && !rec->isStatic && (rec->containingType || rec->primitiveReceiverType);
		}

		// What a receiver with its access can call (D-31): a read-only receiver only const methods; a writable
		// one passes over a const method whose non-const twin (the same parameters) is also a candidate, so
		// `T& At(i)` wins over `const T& At(i) const`. `dropped` reports a non-const method a read-only receiver
		// could not call.
		template <typename Candidate, typename RecordOf, typename AccessOf>
		Core::Vector<Candidate> ByReceiverAccess(const Core::Vector<Candidate>& in, RecordOf recordOf, AccessOf accessOf, bool& dropped)
		{
			auto hasNonConstTwin = [&](uint32 i) {
				for (uint32 j = 0; j < in.Count(); ++j)
				{
					const FunctionRecord* other = recordOf(in[j]);
					if (j != i && IsInstanceMethod(other) && !other->isConst && SameParameters(other->type, recordOf(in[i])->type)) return true;
				}
				return false;
			};
			Core::Vector<Candidate> kept;
			for (uint32 i = 0; i < in.Count(); ++i)
			{
				const FunctionRecord* rec = recordOf(in[i]);
				if (IsInstanceMethod(rec) && accessOf(in[i]) == ReceiverAccess::ReadOnly && !rec->isConst) { dropped = true; continue; }
				if (IsInstanceMethod(rec) && accessOf(in[i]) == ReceiverAccess::Writable && rec->isConst && hasNonConstTwin(i)) continue;
				kept.Push(in[i]);
			}
			return kept;
		}

		QueryResult<void> NotConstError(const Core::String& name)
		{
			return QueryResult<void>::Fail(QueryError::ReadOnly, "'" + name + "' is not a const method, so it cannot be called on a const object");
		}
	}

	QueryResult<void> SymbolQueries::SelectByReceiverAccess(Core::Vector<OverloadCandidate>& candidates, ReceiverAccess access, const Core::String& name) const
	{
		bool dropped = false;
		Core::Vector<OverloadCandidate> kept = ByReceiverAccess(candidates,
			[](const OverloadCandidate& c) { return c.record; }, [&](const OverloadCandidate&) { return access; }, dropped);
		if (kept.Empty() && dropped) return NotConstError(name);
		candidates = kept;
		return QueryResult<void>::Success();
	}

	// FUNCTION QUERIES (doc 08): ONE verb. `receiver` null = unqualified name; non-null = "must
	// exist on this type". Overloads resolve here against argTypes; the caller gets one signature.
	// ---------------------------------------------------------------------------
	QueryResult<const FunctionType*> SymbolQueries::QueryFunction(const ProgramModel& model, const Core::String& name,
		const Type* receiver, const TemplateArgumentListNode* tmplArgs,
		const Core::Vector<const Type*>& argTypes, const LookupContext& ctx,
		const FunctionRecord** outRecord) const
	{
		if (!model.m_Valid) return QueryResult<const FunctionType*>::Fail(QueryError::NotCallable, "model not built");

		bool isOperatorName = name.rfind("operator_", 0) == 0; // `a.operator_add(b)` / custom operators
		Core::String annotation = isOperatorName ? name.substr(9) : "";

		Core::Vector<OverloadCandidate> candidates;
		Core::Vector<Core::String> reasons; // why function templates of the name could not be candidates
		const Type* receiverType = receiver ? TypeHandle::UnwrapToValue(receiver) : nullptr;
		bool haveReceiver = receiverType && receiverType->GetKind() == TypeKind::Object;
		TemplateCall templates;
		templates.name = name;
		templates.written = tmplArgs;
		templates.argTypes = &argTypes;
		templates.unitId = ctx.unitId;
		templates.scope = ctx.scope;

		if (haveReceiver)
		{
			// Own methods first, then every base sub-object (most-derived wins on a signature clash), then
			// extension methods.
			const auto* recvObj = static_cast<const ObjectType*>(receiverType);
			CollectMethods(model, recvObj, name, isOperatorName, annotation, candidates);
			if (auto clash = InheritedSignatureClash(recvObj, candidates, name); !clash.Ok())
				return QueryResult<const FunctionType*>::Fail(clash.error, clash.message);
			auto extIt = model.m_ExtensionMethods.find(recvObj);
			if (extIt != model.m_ExtensionMethods.end())
				for (const FunctionRecord* rec : extIt->second)
					// Filter by NAME exactly like the own-methods loop: without this, every extension method on
					// the receiver type becomes a candidate for ANY member call - `p.Length()` would see SetXY,
					// Normalized, ... and report "ambiguous". Operator calls match by annotation instead.
					if (isOperatorName ? ContainsAnnotation(rec, annotation) : (rec->type && rec->type->GetName() == name))
						candidates.Push({ rec->type, rec, rec->minArity });
			if (!isOperatorName)
			{
				if (tmplArgs) candidates.Clear(); // a call that writes template arguments names only templates
				FunctionTemplates::CollectMembers(model, templates, recvObj, candidates, reasons);
				templates.receiver = recvObj; // extension templates (D-21)
				FunctionTemplates::Collect(model, templates, candidates, reasons);
			}
			if (candidates.Empty() && !reasons.Empty()) return QueryResult<const FunctionType*>::Fail(QueryError::NotFound, reasons[0]);
			if (auto access = SelectByReceiverAccess(candidates, ctx.receiver, name); !access.Ok())
				return QueryResult<const FunctionType*>::Fail(access.error, access.message);
			// A member access (`a.DoThing()`) does NOT fall through to free functions: either a
			// method/extension matched or the name is not on that type.
			return ResolveAndReturn(model, candidates, argTypes, name, outRecord);
		}
		else if (ctx.enclosingType)
		{
			// Unqualified call inside a type scope: C++ name lookup - a member of the enclosing type
			// HIDES free functions. Walk own + bases; statics and non-statics both qualify (HIR knows
			// whether `this` exists at the call site). Nested types fall out because Inner's class
			// scope nests inside S's, so S's statics are found by the same walk from Inner.
			CollectMethods(model, ctx.enclosingType, name, isOperatorName, annotation, candidates);
			if (tmplArgs) candidates.Clear();
			if (!isOperatorName) FunctionTemplates::CollectMembers(model, templates, ctx.enclosingType, candidates, reasons);
			if (!candidates.Empty())
			{
				if (auto clash = InheritedSignatureClash(ctx.enclosingType, candidates, name); !clash.Ok())
					return QueryResult<const FunctionType*>::Fail(clash.error, clash.message);
				if (auto access = SelectByReceiverAccess(candidates, ctx.receiver, name); !access.Ok())
					return QueryResult<const FunctionType*>::Fail(access.error, access.message);
				return ResolveAndReturn(model, candidates, argTypes, name, outRecord); // a member hides free functions
			}
		}

	// Free functions (always for receiver==null without a member hit; the qualified form works too).
	// A function declared in the body being lowered (doc 06) shadows globals for this name.
	const SymbolInfo* sym = nullptr;
	if (!haveReceiver && name.find("::") == Core::String::npos)
	{
		const SymbolInfo* local = model.FindLocal(name);
		if (local && local->kind == SymbolKind::Function) sym = local;
	}
	if (!sym) sym = model.FindVisibleQualified(name, ctx.unitId, ctx.scope);
		const FunctionRecord* elsewhere = nullptr; // a function of the name this file cannot see
		if (sym)
		{
			if (sym->kind == SymbolKind::Function)
				for (const FunctionRecord* rec : sym->functions)
				{
					// A bare name (no receiver) can only ever refer to a FREE function: an extension
					// method needs a receiver object to be callable, so it must not pollute the candidate
					// set of a same-named free function. Without this, `funcptr<...> f = Add;` (or any
					// zero-arg discovery query) sees {free Add, ext Add} - two candidates, neither viable
					// with no argument context - and the decay fails with "no matching overload".
					if (!haveReceiver && rec->isExtensionMethod) continue;
					// One overload set holds every file's same-named static (D4): each is a candidate only where
					// its declaration is visible, so a file calls its own.
					if (!rec->isBuiltin && !model.Symbols()->DeclarationVisible(*rec, ctx.unitId)) { elsewhere = rec; continue; }
					candidates.Push({ rec->type, rec, rec->minArity });
				}
			else if (!haveReceiver && !isOperatorName)
				return QueryResult<const FunctionType*>::Fail(QueryError::NotCallable, "name is not a function: " + name);
		}
		// A prelude function the program's own declaration hides is the one a file calls where that declaration is
		// not visible (BUG-320).
		if (candidates.Empty() && !haveReceiver && !isOperatorName && name.find("::") == Core::String::npos)
			CollectHiddenPrelude(model, name, candidates);
		if (!sym && name.find("::") == Core::String::npos)
			if (const SymbolInfo* unseen = model.Symbols()->GetRoot()->Find(name); unseen && unseen->kind == SymbolKind::Function && !unseen->functions.Empty())
				elsewhere = unseen->functions[0];
		if (candidates.Empty() && elsewhere && !tmplArgs)
			reasons.Push("no function named " + name + " is visible here: it is declared in " + model.SourceName(elsewhere->declUnit) +
				", which this file does not include");

		// The function templates of the name join the overload set with their written, deduced or default
		// arguments (D-17); a call that writes template arguments names only templates.
		if (tmplArgs) candidates.Clear();
		if (!isOperatorName) FunctionTemplates::Collect(model, templates, candidates, reasons);
		if (candidates.Empty())
			return QueryResult<const FunctionType*>::Fail(QueryError::NotFound, reasons.Empty() ? "no function named " + name : reasons[0]);

		// Discovery query: no argument context was supplied (LSP go-to-definition, "what is Fa's
		// type", a member-access without a call). When exactly one signature exists for the name,
		// return it as the representative - the caller is asking about the symbol, not validating a
		// particular call. A genuine zero-argument call also lands here and resolves normally, since
		// its minArity is 0; only an arity MISMATCH with no context degrades to "pick the one".
		if (argTypes.Empty() && candidates.Count() == 1)
		{
			if (outRecord) *outRecord = candidates[0].record;
			return QueryResult<const FunctionType*>::Success(candidates[0].sig);
		}

		return ResolveAndReturn(model, candidates, argTypes, name, outRecord);
	}

	QueryResult<const FunctionType*> SymbolQueries::ResolveAndReturn(const ProgramModel& model, const Core::Vector<OverloadCandidate>& candidates,
		const Core::Vector<const Type*>& argTypes, const Core::String& name, const FunctionRecord** outRecord) const
	{
		auto resolved = OverloadResolver::Resolve(candidates, argTypes, [&](const FunctionRecord* a, const FunctionRecord* b) {
			return FunctionTemplateOrder(model, a, b);
		});
		if (resolved.status == OverloadResolution::Status::Resolved)
		{
			if (outRecord) *outRecord = resolved.chosenRecord;
			return QueryResult<const FunctionType*>::Success(resolved.chosen);
		}
		if (resolved.status == OverloadResolution::Status::Ambiguous)
			return QueryResult<const FunctionType*>::Fail(QueryError::Ambiguous, resolved.message);
		const Core::String braced = BracedArgumentHint(argTypes);
		if (candidates.Count() == 1 && braced.empty())
			if (const CandidateMismatch why = OverloadResolver::Explain(candidates[0], argTypes, name); !why.message.empty())
			{
				auto failure = QueryResult<const FunctionType*>::Fail(QueryError::NotFound, why.message);
				failure.argument = why.argument;
				return failure;
			}
		return QueryResult<const FunctionType*>::Fail(QueryError::NotFound, "no matching overload of " + name + braced);
	}

	// What a braced argument can bind, when a call that has one matches nothing (D-57).
	Core::String SymbolQueries::BracedArgumentHint(const Core::Vector<const Type*>& argTypes)
	{
		for (const Type* arg : argTypes)
			if (arg && arg->GetKind() == TypeKind::BracedList)
				return ": a braced argument binds an __init_list<T> parameter, an object parameter (by value or const T&) its "
					"entries build - an aggregate with that many fields or a create taking that many arguments - or an array "
					"parameter with at least that many elements";
		return "";
	}

	int SymbolQueries::FunctionTemplateOrder(const ProgramModel& model, const FunctionRecord* a, const FunctionRecord* b)
	{
		// Partial ordering by the templates' parameter types as written (`F(T*)` is more specialized than `F(T)`).
		auto ta = model.m_TemplateOfInstance.find(a);
		auto tb = model.m_TemplateOfInstance.find(b);
		if (ta == model.m_TemplateOfInstance.end() || tb == model.m_TemplateOfInstance.end()) return 0;
		auto patternsOf = [](const ProgramModel::TemplateDef& def) {
			TemplatePatterns p;
			p.params = def.params;
			for (const auto& param : def.functionBody->GetParameters())
				if (param->IsPack()) p.expansion = param->GetType().Get();
				else p.patterns.Push(param->GetType().Get());
			return p;
		};
		return PartialOrdering::Compare(patternsOf(*ta->second), patternsOf(*tb->second));
	}

	void SymbolQueries::CollectMethods(const ProgramModel& model, const ObjectType* type, const Core::String& name,
		bool isOperatorName, const Core::String& annotation, Core::Vector<OverloadCandidate>& out) const
	{
		ClassHierarchy::ForEachSubobject(type, [&](const ObjectType* t, int64) {
			auto recsIt = model.m_MethodRecords.find(t);
			for (uint32 i = 0; i < t->GetMethodCount(); ++i)
			{
				const auto& m = t->GetMethods()[i];
				bool match = isOperatorName ? m.HasOperator(annotation) : m.mName == name;
				if (!match) continue;
				const FunctionRecord* rec = (recsIt != model.m_MethodRecords.end() && i < recsIt->second.Count()) ? recsIt->second[i] : nullptr;
				if (rec) out.Push({ rec->type, rec, rec->minArity });
			}
		});
	}

	QueryResult<void> SymbolQueries::InheritedSignatureClash(const ObjectType* type, const Core::Vector<OverloadCandidate>& candidates,
		const Core::String& name) const
	{
		// Two candidates with one signature from unrelated owners (neither derives from the other) are two
		// different inherited functions: C++ reports the call as ambiguous instead of picking one.
		for (uint32 i = 0; i < candidates.Count(); ++i)
			for (uint32 j = 0; j < i; ++j)
			{
				const ObjectType* a = candidates[i].record ? candidates[i].record->containingType : nullptr;
				const ObjectType* b = candidates[j].record ? candidates[j].record->containingType : nullptr;
				if (!a || !b || a == b || !SameParameters(candidates[i].sig, candidates[j].sig)) continue;
				if (ClassHierarchy::DerivesFrom(a, b) || ClassHierarchy::DerivesFrom(b, a)) continue;
				bool overriddenBelowBoth = false; // a declaration in a type deriving from both settles it
				for (uint32 k = 0; k < candidates.Count() && !overriddenBelowBoth; ++k)
				{
					const ObjectType* c = candidates[k].record ? candidates[k].record->containingType : nullptr;
					overriddenBelowBoth = c && SameParameters(candidates[k].sig, candidates[i].sig) && c != a && c != b &&
						ClassHierarchy::DerivesFrom(c, a) && ClassHierarchy::DerivesFrom(c, b);
				}
				if (overriddenBelowBoth) continue;
				return QueryResult<void>::Fail(QueryError::Ambiguous, "'" + name + "' is inherited from both '" + b->GetName() + "' and '" +
					a->GetName() + "'; declare it in '" + type->GetName() + "' to choose");
			}
		return QueryResult<void>::Success();
	}

	bool SymbolQueries::ContainsAnnotation(const FunctionRecord* rec, const Core::String& annotation)
	{
		for (const auto& a : rec->operatorAnnotations) if (a == annotation) return true;
		return false;
	}
	// ---------------------------------------------------------------------------
	// OPERATOR QUERIES (doc 08): the token as written selects the canonical annotation; a null rhs
	// makes it unary. The left operand's type is the receiver (D-25: operands never swap): its methods,
	// inherited methods and extensions, or a primitive's extensions. Compound assignments prefer the
	// in-place form and fall back to the plain operator.
	// ---------------------------------------------------------------------------
	QueryResult<OperatorBinding> SymbolQueries::QueryOperator(const ProgramModel& model, const Core::String& opToken, const Type* lhs,
		const Type* rhs, const LookupContext& ctx) const
	{
		if (!model.m_Valid) return QueryResult<OperatorBinding>::Fail(QueryError::NotFound, "model not built");

		// The token as written selects the annotation through the ONE operator catalog (doc 08); an
		// already-canonical name ("add", "index", "operator_hash") passes through unchanged.
		Core::String annotation = opToken;
		const char* mapped = OperatorTable::AnnotationFor(opToken, /*unary=*/rhs == nullptr);
		if (mapped) annotation = mapped;

		bool isAssignForm = OperatorTable::IsAssignForm(annotation);
		Core::String plainAnnotation = OperatorTable::PlainFormFor(annotation);

		const Type* lhsType = lhs ? TypeHandle::UnwrapToValue(lhs) : nullptr;
		const Type* rhsType = rhs ? TypeHandle::UnwrapToValue(rhs) : nullptr;

		struct Candidate { const FunctionRecord* rec; const ObjectType::MethodInfo* method; };
		Core::Vector<Candidate> found;

		auto collect = [&]()
		{
			if (!lhsType) return;
			if (lhsType->GetKind() != TypeKind::Object)
			{
				// Two primitives always use the built-in operator; an extension on a primitive serves an object on the right.
				if (!rhsType || rhsType->GetKind() != TypeKind::Object) return;
				auto prim = model.m_PrimitiveExtensions.find(lhsType);
				if (prim == model.m_PrimitiveExtensions.end()) return;
				for (const FunctionRecord* rec : prim->second)
					if (ContainsAnnotation(rec, annotation)) found.Push({ rec, nullptr });
				return;
			}
			ClassHierarchy::ForEachSubobject(static_cast<const ObjectType*>(lhsType), [&](const ObjectType* ty, int64) {
				auto recsIt = model.m_MethodRecords.find(ty);
				for (uint32 i = 0; i < ty->GetMethodCount(); ++i)
				{
					const auto& m = ty->GetMethods()[i];
					if (!m.HasOperator(annotation)) continue;
					const FunctionRecord* rec = (recsIt != model.m_MethodRecords.end() && i < recsIt->second.Count()) ? recsIt->second[i] : nullptr;
					if (rec) found.Push({ rec, &m });
				}
			});
		};

		collect();
		bool usedFallback = false;
		if (found.Empty() && isAssignForm) // no in-place form: fall back to the plain operator
		{
			annotation = plainAnnotation;
			collect();
			usedFallback = !found.Empty();
		}
		if (found.Empty())
			return QueryResult<OperatorBinding>::Fail(QueryError::NotFound, "no operator '" + opToken + "' for these operands");

		// The method takes the right operand (a unary operator takes none). Only methods that can take it are
		// candidates; then a read-only receiver keeps the const ones.
		Core::Vector<const Type*> args;
		if (rhsType) args.Push(rhsType);
		Core::Vector<Candidate> viable;
		for (const auto& c : found)
			if (OverloadResolver::Resolve({ { c.rec->type, c.rec, c.rec->minArity } }, args).status == OverloadResolution::Status::Resolved)
				viable.Push(c);
		if (viable.Empty())
			return QueryResult<OperatorBinding>::Fail(QueryError::NotFound, "no operator '" + opToken + "' for these operands");
		bool dropped = false;
		found = ByReceiverAccess(viable, [](const Candidate& c) { return c.rec; }, [&](const Candidate&) { return ctx.receiver; }, dropped);
		if (found.Empty())
			return dropped ? QueryResult<OperatorBinding>::Fail(QueryError::ReadOnly, "the operator '" + opToken + "' is not a const method, so it cannot be used on a const object")
				: QueryResult<OperatorBinding>::Fail(QueryError::NotFound, "no operator '" + opToken + "' for these operands");

		Core::Vector<OverloadCandidate> cands;
		for (const auto& c : found) cands.Push({ c.rec->type, c.rec, c.rec->minArity });
		auto resolved = OverloadResolver::Resolve(cands, args);
		if (resolved.status == OverloadResolution::Status::Ambiguous)
			return QueryResult<OperatorBinding>::Fail(QueryError::Ambiguous, "the operator '" + opToken + "' is ambiguous: " + resolved.message);
		uint32 pickIndex = 0;
		for (uint32 i = 0; i < cands.Count(); ++i)
			if (cands[i].record == resolved.chosenRecord) { pickIndex = i; break; }

		const Candidate& pick = found[pickIndex];
		OperatorBinding binding;
		binding.method = pick.method;
		binding.record = pick.rec;
		binding.isAssignForm = isAssignForm && !usedFallback; // true only when the in-place form itself matched
		return QueryResult<OperatorBinding>::Success(binding);
	}

	// ---------------------------------------------------------------------------
	// VARIABLE QUERIES (doc 04): ONE verb for local / member / global / static.
	// ---------------------------------------------------------------------------
	QueryResult<VariableInfo> SymbolQueries::QueryVariable(const ProgramModel& model, const Core::String& name,
		const Type* receiver, const LookupContext& ctx, const Scope* scopeContext) const
	{
		if (!model.m_Valid) return QueryResult<VariableInfo>::Fail(QueryError::NotFound, "model not built");

		if (receiver)
		{
			const Type* t = TypeHandle::UnwrapToValue(receiver);
			if (!t || t->GetKind() != TypeKind::Object)
				return QueryResult<VariableInfo>::Fail(QueryError::NotCallable, "not an object type: " + name);
			ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(static_cast<const ObjectType*>(t), name);
			if (!hit.field) return QueryResult<VariableInfo>::Fail(QueryError::NotFound, "no member '" + name + "'");
			VariableInfo v;
			v.type = hit.field->m_Type;
			v.isConst = hit.field->m_IsConst;
			v.isStatic = hit.field->m_IsStatic;
			return QueryResult<VariableInfo>::Success(v); // field: no symbol record
		}

	// Unqualified: local (lowering chain, innermost wins), then a member of the enclosing type
	// (implicit this / static), then a global. C++ order: innermost scope wins.
	if (const SymbolInfo* local = model.FindLocal(name))
	{
		if (local->kind == SymbolKind::Variable)
		{
			VariableInfo v;
			v.type = local->type;
			v.isConst = local->isConst;
			v.isStatic = local->isStatic;
			v.symbol = local;
			v.constValue = local->constValue; // a bound non-type param is a constant, not storage
			return QueryResult<VariableInfo>::Success(v);
		}
		// A local of another kind (a type or function named `name`) shadows the global for this
		// name: it is a variable query, so report NotCallable rather than silently using the global.
		return QueryResult<VariableInfo>::Fail(QueryError::NotCallable, "name is not a variable: " + name);
	}

	if (ctx.enclosingType)
		{
			ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(ctx.enclosingType, name);
			if (hit.field)
			{
				VariableInfo v;
				v.type = hit.field->m_Type;
				v.isConst = hit.field->m_IsConst;
				v.isStatic = hit.field->m_IsStatic;
				return QueryResult<VariableInfo>::Success(v);
			}
		}

		// A frame variable is reachable only through FindLocal: found here, it belongs to an enclosing function
		// whose layers a nested body's scope chain passes through (a lambda reaches it only by capture).
		const SymbolInfo* info = model.FindVisibleQualified(name, ctx.unitId, ctx.scope);
		if (!info || info->IsFrameVariable()) return QueryResult<VariableInfo>::Fail(QueryError::NotFound, "no variable named " + name);
		if (info->kind != SymbolKind::Variable)
			return QueryResult<VariableInfo>::Fail(QueryError::NotCallable, "name is not a variable: " + name);
		VariableInfo v;
		v.type = info->type;
		v.isConst = info->isConst;
		v.isStatic = info->isStatic;
		v.symbol = info;
		v.constValue = info->constValue; // const globals carry their folded value
		return QueryResult<VariableInfo>::Success(v);
	}

	QueryResult<MemberBinding> SymbolQueries::QueryMember(const ProgramModel& model, const Type* base, const Core::String& name) const
	{
		if (!model.m_Valid) return QueryResult<MemberBinding>::Fail(QueryError::NotCallable, "model not built");
		const Type* t = base ? TypeHandle::UnwrapToValue(base) : nullptr;
		if (!t) return QueryResult<MemberBinding>::Fail(QueryError::NotCallable, "members require a type");

		// A built-in aggregate exposes its members directly (e.g. __init_list<T> has data/size).
		if (t->GetKind() == TypeKind::InitList)
		{
			const ObjectType::FieldInfo* f = static_cast<const InitListType*>(t)->FindMember(name);
			if (!f) return QueryResult<MemberBinding>::Fail(QueryError::NotFound, "no member '" + name + "'");
			MemberBinding mb; mb.IsField = true; mb.field = f;
			return QueryResult<MemberBinding>::Success(mb);
		}

		// A named union (`union Value { ... }`) is a UnionType, not an ObjectType, but its members are
		// ordinary FieldInfos that alias shared storage at offset 0 - so member access works exactly like
		// an object field (codegen GEPs by the field offset, which is 0 for every union member).
		if (t->GetKind() == TypeKind::Union)
		{
			for (const auto& f : static_cast<const UnionType*>(t)->GetFields())
				if (f.mName == name)
				{
					MemberBinding mb; mb.IsField = true; mb.field = &f;
					return QueryResult<MemberBinding>::Success(mb);
				}
			return QueryResult<MemberBinding>::Fail(QueryError::NotFound, "no member '" + name + "'");
		}

		if (t->GetKind() != TypeKind::Object)
			return QueryResult<MemberBinding>::Fail(QueryError::NotCallable, "members require an object type");
		const auto* obj = static_cast<const ObjectType*>(t);
		ClassHierarchy::FieldHit hit = ClassHierarchy::FindField(obj, name);
		if (hit.field)
		{
			if (hit.ambiguous)
				return QueryResult<MemberBinding>::Fail(QueryError::Ambiguous, "'" + name + "' is ambiguous in '" + obj->GetName() + "': more than one base declares it");
			MemberBinding mb; mb.IsField = true; mb.field = hit.field; mb.owner = hit.owner; mb.ownerOffset = hit.ownerOffset;
			return QueryResult<MemberBinding>::Success(mb);
		}
		MemberBinding method;
		ClassHierarchy::ForEachSubobject(obj, [&](const ObjectType* ty, int64 offset) {
			if (method.method) return;
			for (const auto& m : ty->GetMethods())
				if (m.mName == name) { method.method = &m; method.owner = ty; method.ownerOffset = offset; return; }
		});
		if (method.method) return QueryResult<MemberBinding>::Success(method);
		return QueryResult<MemberBinding>::Fail(QueryError::NotFound, "no member '" + name + "'");
	}

	// ---------------------------------------------------------------------------
	// SYMBOL / CONSTANT / LAYOUT QUERIES
	// ---------------------------------------------------------------------------
	QueryResult<const SymbolInfo*> SymbolQueries::QuerySymbol(const ProgramModel& model, const Core::String& name, uint32 unitId,
		const Scope* context) const
	{
		if (!model.m_Valid) return QueryResult<const SymbolInfo*>::Fail(QueryError::NotFound, "model not built");
	const SymbolInfo* info = model.FindVisibleQualified(name, unitId, context);
	if (!info) return QueryResult<const SymbolInfo*>::Fail(QueryError::NotFound, "no symbol named " + name);
		return QueryResult<const SymbolInfo*>::Success(info);
	}

	QueryResult<ConstantValue> SymbolQueries::QueryConstant(const ProgramModel& model, const Core::String& name, uint32 unitId,
		const Scope* context) const
	{
		if (!model.m_Valid) return QueryResult<ConstantValue>::Fail(QueryError::NotFound, "model not built");

	// Qualified names ("Math::K", "Color::Green") resolve through the ordinary visibility-aware
	// walk: namespaces descend into their scope, and unscoped-enum members are registered as class-
	// scope const variables, so both land in the general path below. The enum-member form is kept
	// only for SCOPED enums (whose members are not injected into a scope).
	const SymbolInfo* info = model.FindVisibleQualified(name, unitId, context);
	if (!info)
	{
		size_t colon = name.rfind("::");
		if (colon != Core::String::npos)
		{
			const SymbolInfo* typeInfo = model.FindVisibleQualified(name.substr(0, colon), unitId, context);
			if (typeInfo && typeInfo->kind == SymbolKind::Type && typeInfo->type &&
			    typeInfo->type->GetKind() == TypeKind::Enum)
			{
				const auto* e = static_cast<const EnumType*>(TypeHandle::UnwrapToValue(typeInfo->type));
				for (const auto& m : e->GetMembers())
					if (m.mName == name.substr(colon + 2))
						return QueryResult<ConstantValue>::Success(ConstantValue::Int(m.m_Value));
			}
		}
		return QueryResult<ConstantValue>::Fail(QueryError::NotFound, "no constant named " + name);
	}
		if (info->kind != SymbolKind::Variable)
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + name + "' is not a value");
		if (!info->isConst)
			return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + name + "' is a variable: only a const with a constant initializer folds");
		if (!info->constValue.IsNone()) return QueryResult<ConstantValue>::Success(info->constValue);
		// An unscoped enum member is registered as a const variable whose value lives on the type.
		// The name may be qualified ("Math::Green"); the member matches its final segment.
		if (info->type && info->type->GetKind() == TypeKind::Enum)
		{
			size_t colon = name.rfind("::");
			const Core::String& memberName = (colon == Core::String::npos) ? name : name.substr(colon + 2);
			const auto* e = static_cast<const EnumType*>(TypeHandle::UnwrapToValue(info->type));
			for (const auto& m : e->GetMembers())
				if (m.mName == memberName) return QueryResult<ConstantValue>::Success(ConstantValue::Int(m.m_Value));
		}
		return QueryResult<ConstantValue>::Fail(QueryError::NotAValue, "'" + name + "' is a const whose initializer runs at startup");
	}


} // namespace Scripting


