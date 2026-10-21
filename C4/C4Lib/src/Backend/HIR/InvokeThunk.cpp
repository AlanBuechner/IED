#include "Backend/HIR/InvokeThunk.h"
#include "Backend/HIR/CallArguments.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LoweringContext.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/FunctionType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/VirtualTables.h"

namespace Scripting
{
	namespace
	{
		const Type* VoidPointer(TypeStore* types) { return types->Pointer(types->Builtin(TypeKind::Void)); }

		// The thunk's own parameters, slots 0 to 3 in signature order.
		enum ThunkSlot : uint32 { kCode = 0, kObject = 1, kResult = 2, kArguments = 3 };

		class ThunkBuilder
		{
		public:
			ThunkBuilder(LoweringContext& ctx, const FunctionRecord& target, bool shared)
				: m_Ctx(ctx), m_Target(target), m_Types(ctx.queries().TypeStore()), m_Shared(shared) {}

			QueryResult<void> Build()
			{
				const Type* voidPtr = VoidPointer(m_Types);
				m_Ctx.AllocSlot(voidPtr, "code", SlotClass::Param);
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
				if (m_Shared) call->code = Load(kCode);
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
			bool m_Shared;
		};

		bool DispatchesOnObject(const ProgramModel& model, const FunctionRecord& target)
		{
			if (!target.containingType || target.isStatic) return false;
			const ObjectType::MethodInfo* method = model.MethodForRecord(target);
			return method && VirtualTables::IsVirtual(target.containingType, *method);
		}

		const char* ReceiverText(const FunctionRecord& target)
		{
			if ((!target.containingType && !target.primitiveReceiverType) || target.isStatic) return "free";
			return target.isExtensionMethod || target.primitiveReceiverType ? "extension" : "member";
		}

		// How a thunk passes a value of `type`: every pointer alike, every reference alike, a scalar by its kind and
		// width, and anything else (an object's copy and delete run in the thunk) by its own type.
		Core::String PassedAs(const Type* type)
		{
			if (!type) return "void";
			switch (type->GetKind())
			{
			case TypeKind::Alias: return PassedAs(static_cast<const AliasType*>(type)->GetAliasedType());
			case TypeKind::Reference: return "&";
			case TypeKind::Pointer: case TypeKind::FunPtr: return "*";
			case TypeKind::Void: case TypeKind::Bool: return QualifiedName(type);
			case TypeKind::Float32: case TypeKind::Float64: return "f" + std::to_string(type->GetSize() * 8);
			case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
			case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
			case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
			case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
			case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32: case TypeKind::Enum:
				return "i" + std::to_string(type->GetSize() * 8);
			default: return QualifiedName(type);
			}
		}
	}

	const FunctionType* InvokeThunkType(ProgramModel& model)
	{
		TypeStore* types = model.TypeStorePtr();
		const Type* voidPtr = VoidPointer(types);
		return types->Function(types->Builtin(TypeKind::Void), { voidPtr, voidPtr, voidPtr, types->Pointer(voidPtr) });
	}

	bool Invokable(const FunctionRecord& target)
	{
		return target.type && !target.type->IsVariadic() && !target.primitiveReceiverType && !target.isLocal;
	}

	bool SharesInvokeThunk(const ProgramModel& model, const FunctionRecord& target)
	{
		return !target.isLinked && !DispatchesOnObject(model, target);
	}

	Core::String InvokeSignature(const FunctionRecord& target)
	{
		Core::String text = Core::String(ReceiverText(target)) + (target.isNoexcept ? " noexcept " : " ") + PassedAs(target.type->GetReturnType()) + "(";
		const auto& parameters = target.type->GetParameters();
		for (uint32 i = 0; i < parameters.Count(); ++i) text += Core::String(i ? ", " : "") + PassedAs(parameters[i].m_Type);
		return text + ")";
	}

	HIRFunction LowerInvokeThunk(ProgramModel& model, ErrorTracker& errors, const FunctionRecord& target, const FunctionRecord& thunk, bool shared)
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
		fn.complete = ThunkBuilder(ctx, target, shared).Build().Ok() && errors.GetErrors().Empty();
		return fn;
	}
} // namespace Scripting
