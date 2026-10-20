#include "Backend/HIR/InvokeThunk.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunctionType.h"

namespace Scripting
{
	namespace
	{
		const Type* VoidPointer(TypeStore* types) { return types->Pointer(types->Builtin(TypeKind::Void)); }

		// The thunk's own parameters, slots 0, 1 and 2 in signature order.
		enum ThunkSlot : uint32 { kObject = 0, kResult = 1, kArguments = 2 };

		class ThunkBuilder
		{
		public:
			ThunkBuilder(LoweringContext& ctx, const FunctionRecord& target) : m_Ctx(ctx), m_Target(target), m_Types(ctx.queries().TypeStore()) {}

			QueryResult<void> Build()
			{
				const Type* voidPtr = VoidPointer(m_Types);
				m_Ctx.AllocSlot(voidPtr, "object", SlotClass::Param);
				m_Ctx.AllocSlot(voidPtr, "result", SlotClass::Param);
				m_Ctx.AllocSlot(m_Types->Pointer(voidPtr), "arguments", SlotClass::Param);
				BlockNode* root = m_Ctx.out->root;
				return Lifecycle(m_Ctx).LowerStatement(root, [&]() -> QueryResult<void>
					{
						auto call = Call();
						if (!call.Ok()) return QueryResult<void>::Fail(call.error, call.message);
						root->stmts.push_back(Deliver(call.value));
						return QueryResult<void>::Success();
					});
			}

		private:
			QueryResult<CallNode*> Call()
			{
				auto* call = m_Ctx.out->NewNode<CallNode>(m_Target.type->GetReturnType(), &m_Target);
				if (m_Target.containingType && !m_Target.isStatic)
				{
					auto bound = BindReceiver(m_Ctx, call, Pointee(Load(kObject), m_Target.containingType));
					if (!bound.Ok()) return QueryResult<CallNode*>::Fail(bound.error, bound.message);
				}
				const auto& parameters = m_Target.type->GetParameters();
				for (uint32 i = 0; i < parameters.Count(); ++i)
				{
					const Type* declared = parameters[i].m_Type;
					const LValue* argument = Pointee(ArgumentAddress(i), TypeHandle::UnwrapToValue(declared));
					auto bound = BindArgument(m_Ctx, Value::L(argument), m_Ctx.LoadIfNeeded(Value::L(argument)), declared);
					if (!bound.Ok()) return QueryResult<CallNode*>::Fail(bound.error, bound.message);
					call->args.push_back(bound.value);
				}
				return QueryResult<CallNode*>::Success(call);
			}

			const HIRNode* Load(ThunkSlot slot)
			{
				const Type* voidPtr = VoidPointer(m_Types);
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = voidPtr; lv.slot = slot;
				return m_Ctx.out->NewNode<LoadNode>(voidPtr, m_Ctx.NewLValue(lv));
			}

			// `*(T*)address`: the object an untyped address points at.
			const LValue* Pointee(const HIRNode* address, const Type* type)
			{
				LValue lv; lv.kind = LValue::Kind::Deref; lv.type = type;
				lv.addr = m_Ctx.out->NewNode<ConvertNode>(address, address->type, m_Types->Pointer(type));
				return m_Ctx.NewLValue(lv);
			}

			// arguments[index]
			const HIRNode* ArgumentAddress(uint32 index)
			{
				const Type* voidPtr = VoidPointer(m_Types);
				LValue slot; slot.kind = LValue::Kind::Slot; slot.type = m_Types->Pointer(voidPtr); slot.slot = kArguments;
				const HIRNode* position = m_Ctx.out->NewNode<ConstantNode>(m_Types->Builtin(TypeKind::Int64), ConstantValue::Int(index));
				const LValue* element = BuildIndexLValue(m_Ctx, Value::L(m_Ctx.NewLValue(slot)), position, voidPtr, sizeof(void*));
				return m_Ctx.out->NewNode<LoadNode>(voidPtr, element);
			}

			// The call's value goes to `result`: built there, or (a reference result) its address stored there.
			HIRNode* Deliver(const CallNode* call)
			{
				const Type* returned = m_Target.type->GetReturnType();
				if (!returned || TypeHandle::UnwrapToValue(returned)->GetKind() == TypeKind::Void)
					return m_Ctx.out->NewNode<ExprStmtNode>(call);
				const Type* voidPtr = VoidPointer(m_Types);
				if (returned->GetKind() == TypeKind::Reference)
				{
					const HIRNode* address = m_Ctx.out->NewNode<ConvertNode>(call, returned, voidPtr);
					return m_Ctx.out->NewNode<StoreNode>(Pointee(Load(kResult), voidPtr), address);
				}
				return m_Ctx.out->NewNode<StoreNode>(Pointee(Load(kResult), TypeHandle::UnwrapToValue(returned)), call);
			}

			LoweringContext& m_Ctx;
			const FunctionRecord& m_Target;
			TypeStore* m_Types;
		};
	}

	const FunctionType* InvokeThunkType(ProgramModel& model)
	{
		TypeStore* types = model.TypeStorePtr();
		const Type* voidPtr = VoidPointer(types);
		return types->Function(types->Builtin(TypeKind::Void), { voidPtr, voidPtr, types->Pointer(voidPtr) });
	}

	bool Invokable(const FunctionRecord& target)
	{
		return target.type && !target.type->IsVariadic() && !target.primitiveReceiverType && !target.isLocal;
	}

	HIRFunction LowerInvokeThunk(ProgramModel& model, ErrorTracker& errors, const FunctionRecord& target, const FunctionRecord& thunk)
	{
		HIRFunction fn;
		fn.record = &thunk;
		LoweringContext ctx(&model, &errors);
		ctx.fn = &thunk;
		ctx.out = &fn;
		ctx.unitId = target.definingUnit;
		ctx.scope = target.scope;
		ScopeGuard frame = ctx.queries().PushScope(Scope::Kind::Function, thunk.mangledName);
		fn.root = fn.NewNode<BlockNode>(nullptr);
		fn.complete = ThunkBuilder(ctx, target).Build().Ok() && errors.GetErrors().Empty();
		return fn;
	}
} // namespace Scripting
