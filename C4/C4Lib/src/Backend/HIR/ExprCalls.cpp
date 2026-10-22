#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/CallResults.h"
#include <tuple>
#include "Backend/HIR/AssertionSites.h"
#include "Backend/HIR/ReadOnly.h"
#include "Backend/HIR/IndirectCall.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/Intrinsics.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Backend/HIR/LambdaLowerer.h"
#include "Backend/HIR/Callables.h"
#include "Backend/HIR/FoldLowerer.h"
#include "Backend/HIR/PackElements.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/VarArgs.h"
#include "Backend/HIR/Atomics.h"
#include "Backend/HIR/BitIntrinsics.h"
#include "Backend/HIR/HintIntrinsics.h"
#include "Backend/HIR/SimdIntrinsics.h"
#include "Backend/HIR/BuiltinOperators.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Model/Types/ClassHierarchy.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/BracedInitializerNode.h" // init-list call arguments
#include "Model/Types/ReturnLists.h"
#include "Model/Types/QualifiedName.h"
#include "Parser/Nodes/Declarations/NamespaceQualifiedNameNode.h"
#include "Model/Types/ReferenceType.h" // T& return -> Deref lvalue
#include "Model/TypeHandle.h"

namespace Scripting
{
	QueryResult<Value> ExprLowerer::ResolveCall(const FunctionCallNode* n)
	{
		auto target = ResolveCallTarget(n->GetCallee().Get());
		if (!target.Ok()) return QueryResult<Value>::Fail(target.error, target.message);
		auto arguments = ResolveCallArguments(n->GetArguments());
		if (!arguments.Ok()) return QueryResult<Value>::Fail(arguments.error, arguments.message);
		if (target.value.signature) return CallIndirect(n, target.value, arguments.value);
		const FunctionRecord* rec = nullptr;
		auto sig = ResolveCallee(n, target.value, arguments.value, rec);
		if (!sig.Ok()) return CallWithoutFunction(n, target.value, arguments.value, sig);
		if (!rec) return m_Ctx.Fail(n->GetCallee().Get(), QueryResult<Value>::Fail(QueryError::NotFound, "no lowering record for " + target.value.name));
		return CallFunction(n, target.value, arguments.value, *rec, *sig.value);
	}

	// Through a funcptr or func: no name lookup and no record; the callee is the value itself.
	QueryResult<Value> ExprLowerer::CallIndirect(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args)
	{
		if (auto named = BindTemplateNames(n, args, target.signature->GetParameterTypes()); !named.Ok())
			return QueryResult<Value>::Fail(named.error, named.message);
		if (auto built = MaterializeBracedArguments(args.braced, target.signature->GetParameterTypes(), args.nodes); !built.Ok())
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(built.error, built.message));
		return LowerIndirectCall(m_Ctx, n, target.indirect, target.signature, args.nodes, args.values);
	}

	QueryResult<const FunctionType*> ExprLowerer::ResolveCallee(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args,
		const FunctionRecord*& rec)
	{
		const TemplateArgumentListNode* written = n->GetTemplateArgs() ? n->GetTemplateArgs().Get() : nullptr;
		const ReceiverAccess access = target.receiverLoc ? AccessOf(Value::L(target.receiverLoc)) : ImplicitReceiverAccess();
		auto sig = m_Ctx.queries().QueryFunction(target.name, target.receiverType, written, args.types, m_Ctx.unitId, m_Ctx.enclosingType,
			m_Ctx.scope, &rec, access);
		if (sig.Ok() || target.receiverType || written) return sig;
		return RetryWithNullPointers(target.name, args, rec, sig);
	}

	// A zero integer literal binds to a pointer parameter as null (`Area(0)` like `Area(null)`), tried only when
	// nothing matches the arguments as written.
	QueryResult<const FunctionType*> ExprLowerer::RetryWithNullPointers(const Core::String& name, CallArgumentList& args, const FunctionRecord*& rec,
		const QueryResult<const FunctionType*>& unmatched)
	{
		const Type* voidPtr = m_Ctx.queries().TypeStore()->Pointer(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Void));
		Core::Vector<const Type*> retryTypes = args.types;
		bool anyZero = false;
		for (size_t i = 0; i < retryTypes.Count(); ++i)
			if (IsZeroLiteral(retryTypes[i], args.nodes[i])) { retryTypes[i] = voidPtr; anyZero = true; }
		if (!anyZero) return unmatched;
		const FunctionRecord* retryRec = nullptr;
		auto retry = m_Ctx.queries().QueryFunction(name, nullptr, nullptr, retryTypes, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope, &retryRec);
		if (!retry.Ok()) return unmatched;
		for (size_t i = 0; i < args.nodes.size(); ++i)
			if (retryTypes[i] == voidPtr && IsZeroLiteral(args.types[i], args.nodes[i]))
				args.nodes[i] = m_Ctx.out->NewNode<ConstantNode>(voidPtr, static_cast<const ConstantNode*>(args.nodes[i])->value);
		rec = retryRec;
		return retry;
	}

	bool ExprLowerer::IsZeroLiteral(const Type* type, const HIRNode* node)
	{
		const auto* constant = dynamic_cast<const ConstantNode*>(node);
		return type && type->GetKind() == TypeKind::Int32 && constant && constant->value.kind == ConstantValue::Kind::Int64 && constant->value.intValue == 0;
	}

	// No function matches: a lifecycle, va_list, atomic, SIMD or bit intrinsic, or `T(args)` constructing the object type `T`; otherwise
	// the error, at the argument it names when it names one.
	QueryResult<Value> ExprLowerer::CallWithoutFunction(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args,
		const QueryResult<const FunctionType*>& unmatched)
	{
		const AstNode* callee = n->GetCallee().Get();
		const bool notFound = unmatched.error == QueryError::NotFound && !target.receiverType;
		if (HasTemplateNames(args)) return m_Ctx.Fail(callee, QueryResult<Value>::Fail(unmatched.error, unmatched.message));
		if (notFound && IsLifecycleIntrinsic(target.name)) return m_Ctx.Fail(callee, LowerLifecycleIntrinsic(m_Ctx, target.name, args.values));
		if (notFound && VarArgs::IsIntrinsic(target.name)) return m_Ctx.Fail(callee, VarArgs::LowerIntrinsic(m_Ctx, target.name, args.values));
		if (notFound && Atomics::IsIntrinsic(target.name)) return m_Ctx.Fail(callee, Atomics::LowerIntrinsic(m_Ctx, target.name, args.values));
		if (notFound && SimdIntrinsics::IsIntrinsic(target.name)) return m_Ctx.Fail(callee, SimdIntrinsics::LowerIntrinsic(m_Ctx, target.name, args.values));
		if (notFound && BitIntrinsics::IsIntrinsic(target.name)) return m_Ctx.Fail(callee, BitIntrinsics::LowerIntrinsic(m_Ctx, target.name, args.values));
		if (notFound && HintIntrinsics::IsIntrinsic(target.name)) return m_Ctx.Fail(callee, HintIntrinsics::LowerIntrinsic(m_Ctx, target.name, args.values));
		// Ambiguous and inaccessible calls are errors in their own right, never masked by a type of the same name.
		if (unmatched.error == QueryError::NotFound || unmatched.error == QueryError::NotCallable)
			if (const ObjectType* type = ConstructedType(n, target.name)) return ConstructorCall(callee, type, args);
		const bool atArgument = unmatched.argument >= 0 && args.nodes.size() == n->GetArguments().Count() &&
			(uint32)unmatched.argument < n->GetArguments().Count();
		return m_Ctx.Fail(atArgument ? n->GetArguments()[(uint32)unmatched.argument].Get() : callee,
			QueryResult<Value>::Fail(unmatched.error, unmatched.message));
	}

	const ObjectType* ExprLowerer::ConstructedType(const FunctionCallNode* n, const Core::String& name)
	{
		const TemplateArgumentListNode* written = n->GetTemplateArgs() ? n->GetTemplateArgs().Get() : nullptr;
		auto type = written ? m_Ctx.queries().QueryTemplateInstance(name, *written, m_Ctx.unitId, m_Ctx.scope)
			: m_Ctx.queries().QueryType(name, m_Ctx.unitId, m_Ctx.scope);
		return type.Ok() && type.value ? dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(type.value)) : nullptr;
	}

	// `T(args)` makes a temporary `T` through its create operator; `T({a, b})` is `T{a, b}`.
	QueryResult<Value> ExprLowerer::ConstructorCall(const AstNode* callee, const ObjectType* type, const CallArgumentList& args)
	{
		if (args.braced.size() == 1 && args.braced[0]) return ResolveBracedObject(callee, type, args.braced[0]);
		std::vector<ConstructionArgument> construction;
		for (size_t i = 0; i < args.values.size(); ++i)
		{
			ConstructionArgument argument;
			if (args.braced[i]) argument.brace = args.braced[i];
			else argument.value = args.values[i].isLValue || args.values[i].IsRValue() ? args.values[i] : Value::R(args.nodes[i], args.nodes[i]->type);
			construction.push_back(argument);
		}
		return ResolveConstruct(callee, type, construction, false);
	}

	QueryResult<Value> ExprLowerer::CallFunction(const FunctionCallNode* n, const CallTarget& target, CallArgumentList& args, const FunctionRecord& rec,
		const FunctionType& sig)
	{
		const AstNode* callee = n->GetCallee().Get();
		if (auto allowed = AccessCheck::CheckCall(m_Ctx, rec); !allowed.Ok())
			return m_Ctx.Fail(callee, QueryResult<Value>::Fail(allowed.error, allowed.message));
		Core::Vector<const Type*> params;
		for (const auto& p : sig.GetParameters()) params.Push(p.m_Type);
		if (auto built = MaterializeBracedArguments(args.braced, params, args.nodes); !built.Ok()) return QueryResult<Value>::Fail(built.error, built.message);
		if (auto named = BindTemplateNames(callee, args, params); !named.Ok()) return QueryResult<Value>::Fail(named.error, named.message);
		if (auto bound = BindCallArguments(n, args, sig); !bound.Ok()) return QueryResult<Value>::Fail(bound.error, bound.message);
		if (auto filled = AppendDefaultArguments(callee, target.name, args.nodes, rec, sig); !filled.Ok())
			return QueryResult<Value>::Fail(filled.error, filled.message);
		auto receiver = CallReceiver(n, target, rec);
		if (!receiver.Ok()) return QueryResult<Value>::Fail(receiver.error, receiver.message);
		auto call = m_Ctx.out->NewNode<CallNode>(sig.GetReturnType(), &rec);
		// The receiver moves to the sub-object that declares the method; a virtual method dispatches through the vptr
		// that holds its slot, except in a qualified call `Base::F()`, which names one implementation.
		const CallDispatch dispatch = target.qualified ? CallDispatch::Direct : CallDispatch::Dynamic;
		if (auto bound = BindReceiver(m_Ctx, call, receiver.value, dispatch); !bound.Ok())
			return m_Ctx.Fail(callee, QueryResult<Value>::Fail(bound.error, bound.message));
		for (const HIRNode* a : args.nodes) call->args.push_back(a);
		LocateAssertion(m_Ctx, call, n);
		return CallResult(m_Ctx, call, sig.GetReturnType());
	}

	// Each argument binds to its parameter (CallArguments): references take the address, by-value objects a copy of an
	// lvalue (a fresh object relocates), scalars convert to the parameter type.
	QueryResult<void> ExprLowerer::BindCallArguments(const FunctionCallNode* n, CallArgumentList& args, const FunctionType& sig)
	{
		const bool written = args.nodes.size() == n->GetArguments().Count(); // no pack expanded into several
		for (size_t i = 0; i < args.nodes.size() && i < sig.GetParameterCount(); ++i)
		{
			if (!args.nodes[i]) continue;
			const Type* param = sig.GetParameters()[i].m_Type;
			const Value original = i < args.values.size() ? args.values[i] : Value();
			if (written && !args.braced[i])
				ImplicitConversion::WarnIfNarrowing(m_Ctx, n->GetArguments()[(uint32)i].Get(),
					original.type ? original : Value::R(args.nodes[i], args.nodes[i]->type), param);
			auto bound = BindArgument(m_Ctx, original, args.nodes[i], param);
			if (!bound.Ok()) return m_Ctx.Fail(n->GetCallee().Get(), QueryResult<void>::Fail(bound.error, bound.message));
			args.nodes[i] = bound.value;
		}
		return QueryResult<void>::Success();
	}

	// The trailing parameters a call leaves out take their default argument, evaluated per call where the function is
	// declared; one without a default is an arity error.
	QueryResult<void> ExprLowerer::AppendDefaultArguments(const AstNode* callee, const Core::String& name, std::vector<const HIRNode*>& nodes,
		const FunctionRecord& rec, const FunctionType& sig)
	{
		for (uint32 i = (uint32)nodes.size(); i < sig.GetParameterCount(); ++i)
		{
			const FunctionType::ParameterInfo& param = sig.GetParameters()[i];
			if (!param.m_Type || param.m_IsVariadic) break; // a variadic tail has no defaults
			if (rec.paramDefaults.Count() <= i || !rec.paramDefaults[i])
				return m_Ctx.Fail(callee, QueryResult<void>::Fail(QueryError::NotFound,
					"too few arguments for '" + name + "': parameter " + std::to_string((int)(i + 1)) + " has no default value"));
			auto value = DefaultArgument(rec, i);
			if (!value.Ok()) return m_Ctx.Fail(callee, QueryResult<void>::Fail(value.error, value.message));
			auto bound = BindArgument(m_Ctx, value.value, m_Ctx.LoadIfNeeded(value.value), param.m_Type);
			if (!bound.Ok()) return m_Ctx.Fail(callee, QueryResult<void>::Fail(bound.error, bound.message));
			nodes.push_back(bound.value);
		}
		return QueryResult<void>::Success();
	}

	// The object a method call runs on: the written receiver, or `this` for an unqualified call of an instance method
	// of the enclosing type (`AppendByte('0')` inside StringBuilder::AppendInt).
	QueryResult<const LValue*> ExprLowerer::CallReceiver(const FunctionCallNode* n, const CallTarget& target, const FunctionRecord& rec)
	{
		if (target.receiverLoc || rec.isStatic || !rec.containingType || !m_Ctx.enclosingType) return target.receiverLoc;
		if (m_Ctx.InStaticMethod())
			return m_Ctx.Fail(n, QueryResult<const LValue*>::Fail(QueryError::NotAValue, m_Ctx.NoObject("'" + rec.name + "' is not static")));
		return m_Ctx.ReceiverLValue();
	}

	// Each braced argument becomes the value of the parameter the call bound it to (D-57): an `__init_list<T>` (or a
	// pointer to one), or an object or array built from its entries.
	QueryResult<void> ExprLowerer::MaterializeBracedArguments(const std::vector<const BracedInitializerNode*>& braced,
		const Core::Vector<const Type*>& params, std::vector<const HIRNode*>& argNodes)
	{
		for (size_t i = 0; i < braced.size() && i < params.Count(); ++i)
		{
			if (!braced[i] || !params[(uint32)i]) continue;
			const Type* param = TypeHandle::UnwrapToValue(params[(uint32)i]);
			const Type* pointee = param->GetKind() == TypeKind::Pointer ? static_cast<const PointerType*>(param)->GetPointeeType() : nullptr;
			const bool list = param->GetKind() == TypeKind::InitList || (pointee && pointee->GetKind() == TypeKind::InitList);
			auto value = list ? ResolveInitList(braced[i], param) : ResolveBracedAs(braced[i], param);
			if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message);
			argNodes[i] = m_Ctx.LoadIfNeeded(value.value);
		}
		return QueryResult<void>::Success();
	}

	QueryResult<ExprLowerer::CallArgumentList> ExprLowerer::ResolveCallArguments(const Core::Vector<Ref<AstNode>>& nodes)
	{
		CallArgumentList list;
		for (const auto& node : nodes)
		{
			if (const auto* brace = dynamic_cast<const BracedInitializerNode*>(node.Get()))
			{
				list.types.Push(m_Ctx.queries().TypeStore()->BracedList(brace->EntryCount()));
				list.nodes.push_back(nullptr);
				list.values.push_back(Value());
				list.braced.push_back(brace);
				continue;
			}
			if (const TemplateNameType* named = TemplateNameOf(node.Get()))
			{
				list.types.Push(named);
				list.nodes.push_back(nullptr); // bound once overload resolution picks its parameter
				list.values.push_back(Value());
				list.braced.push_back(nullptr);
				continue;
			}
			auto values = ResolveArgument(node.Get());
			if (!values.Ok()) return QueryResult<CallArgumentList>::Fail(values.error, values.message);
			for (const Value& value : values.value) AppendCallArgument(list, value);
		}
		return list;
	}

	// `this` as a value is the receiver's address, a T* as in C++; the object itself is `*this`.
	Value ExprLowerer::ThisAsPointer(const Value& value)
	{
		if (!value.isLValue || !IsThisObject(value.lvalue)) return value;
		const Type* pointer = AddressType(m_Ctx, value.lvalue);
		return Value::R(m_Ctx.out->NewNode<AddressOfNode>(pointer, value.lvalue), pointer);
	}

	void ExprLowerer::AppendCallArgument(CallArgumentList& list, const Value& value)
	{
		list.braced.push_back(nullptr);
		if (value.isLValue && IsThisObject(value.lvalue))
		{
			const Value pointer = ThisAsPointer(value);
			list.types.Push(pointer.type);
			list.nodes.push_back(m_Ctx.LoadIfNeeded(pointer));
			list.values.push_back(Value());
			return;
		}
		list.types.Push(TypeHandle::UnwrapToValue(value.type));
		list.nodes.push_back(m_Ctx.LoadIfNeeded(value));
		list.values.push_back(value);
	}

	QueryResult<void> ExprLowerer::BindTemplateNames(const AstNode* site, CallArgumentList& list, const Core::Vector<const Type*>& params)
	{
		for (uint32 i = 0; i < list.types.Count(); ++i)
		{
			const auto* named = dynamic_cast<const TemplateNameType*>(list.types[i]);
			if (!named) continue;
			const auto* signature = i < params.Count() ? dynamic_cast<const SignatureType*>(TypeHandle::UnwrapToValue(params[i])) : nullptr;
			if (!signature)
				return m_Ctx.Fail(site, QueryResult<void>::Fail(QueryError::NotAValue, "argument " + std::to_string(i + 1) + " names the " +
					named->GetName() + ", but only a funcptr or func parameter says which instance it is: write its template arguments"));
			auto address = TemplateAddress(site, *named, *signature);
			if (!address.Ok()) return QueryResult<void>::Fail(address.error, address.message);
			list.nodes[i] = address.value.rvalue;
			list.values[i] = address.value;
		}
		return QueryResult<void>::Success();
	}

	bool ExprLowerer::HasTemplateNames(const CallArgumentList& list)
	{
		for (const Type* t : list.types)
			if (dynamic_cast<const TemplateNameType*>(t)) return true;
		return false;
	}

	const TemplateNameType* ExprLowerer::TemplateNameOf(const AstNode* node)
	{
		Core::String name;
		if (const auto* id = dynamic_cast<const IdentifierExpressionNode*>(node)) name = id->GetName();
		else if (const auto* qualified = dynamic_cast<const StaticAccessNode*>(node); !qualified || !QualifiedCallName(qualified, name)) return nullptr;
		if (name.empty() || m_Ctx.queries().FindLocal(name)) return nullptr;
		const TemplateNameType* named = m_Ctx.queries().QueryTemplateName(name, m_Ctx.unitId, m_Ctx.scope);
		if (!named || m_Ctx.queries().QueryVariable(name, nullptr, m_Ctx.unitId, m_Ctx.enclosingType, m_Ctx.scope).Ok()) return nullptr;
		const bool function = m_Ctx.queries().QueryFunction(name, nullptr, nullptr, Core::Vector<const Type*>(), m_Ctx.unitId, m_Ctx.enclosingType,
			m_Ctx.scope).Ok();
		return function ? nullptr : named;
	}

	QueryResult<Value> ExprLowerer::TemplateAddress(const AstNode* site, const TemplateNameType& named, const SignatureType& target)
	{
		auto instance = named.Instance(target);
		if (!instance.Ok()) return m_Ctx.Fail(site, QueryResult<Value>::Fail(instance.error, instance.message));
		Core::Vector<const Type*> params;
		for (const auto& p : instance.value->type->GetParameters()) params.Push(p.m_Type);
		const Type* pointer = m_Ctx.queries().TypeStore()->FunPtr(instance.value->type->GetReturnType(), params);
		return Value::R(m_Ctx.out->NewNode<FunctionAddressNode>(pointer, instance.value), pointer);
	}
} // namespace Scripting
