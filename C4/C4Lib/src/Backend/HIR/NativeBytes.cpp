#include "Backend/HIR/NativeBytes.h"
#include "Backend/HIR/EvaluatedOnce.h"
#include "Backend/HIR/ImplicitConversion.h"
#include "Backend/HIR/IndexLValue.h"
#include "Backend/HIR/IndexLoop.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/MethodReceiver.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/AliasType.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/NativeMemory.h"
#include "Model/Types/PointerType.h"
#include "Model/Types/QualifiedName.h"

namespace Scripting::NativeBytes
{
	namespace
	{
		using Node = QueryResult<const HIRNode*>;
		using Result = QueryResult<Value>;

		Result Fail(const Core::String& message) { return Result::Fail(QueryError::NotAValue, message); }

		const Type* Unaliased(const Type* t)
		{
			while (t && t->GetKind() == TypeKind::Alias) t = static_cast<const AliasType*>(t)->GetAliasedType();
			return t;
		}

		const ObjectType* AsObject(const Type* t)
		{
			t = Unaliased(t);
			return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
		}

		const ArrayType* AsArray(const Type* t)
		{
			t = Unaliased(t);
			return t && t->GetKind() == TypeKind::Array ? static_cast<const ArrayType*>(t) : nullptr;
		}

		// Builds the sums: every part a type holds native memory in, added up as int64.
		class Summer
		{
		public:
			explicit Summer(LoweringContext& ctx) : m_Ctx(ctx), m_Types(ctx.queries().TypeStore()) {}

			// The native bytes of the value at `location`, of a type that holds some. `dispatch`: how a virtual operator
			// is called (a field or base sub-object is of its exact type).
			Node At(const LValue* location, CallDispatch dispatch)
			{
				const Type* type = Unaliased(location->type);
				if (const ArrayType* array = AsArray(type))
					return Elements(FirstElement(location, array), Constant(static_cast<int64>(array->GetElementCount())), array->GetElementType());
				const ObjectType* object = AsObject(type);
				if (const ObjectType::MethodInfo* own = NativeMemory::OwnOperator(object)) return OperatorCall(object, *own, location, dispatch);
				return Parts(object, location);
			}

			// The sum over the `count` elements of type `element` from the pointer `first`, both evaluated once.
			Node Elements(const HIRNode* first, const HIRNode* count, const Type* element)
			{
				auto* block = m_Ctx.out->NewNode<BlockNode>(nullptr);
				const LValue* start = Local(block, m_Types->Pointer(element), first, "__native_first");
				const LValue* end = Local(block, Int64(), Convert(count), "__native_count");
				const LValue* total = Local(block, Int64(), Constant(0), "__native_total");
				Node failed = Node::Success(nullptr);
				IndexRange range{ [&] { return Constant(0); }, [&] { return Load(end); } };
				AppendIndexLoop(m_Ctx, block, range, false, [&](BlockNode* body, const HIRNode* index) {
					const LValue* item = BuildIndexLValue(m_Ctx, Value::R(Load(start), start->type), index, element, element->GetSize());
					Node part = At(item, CallDispatch::Direct);
					if (part.Ok()) body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(total, Add(Load(total), part.value)));
					else failed = part;
				});
				if (!failed.Ok()) return failed;
				auto* sum = m_Ctx.out->NewNode<SequenceNode>(Int64());
				sum->steps.push_back(block);
				sum->steps.push_back(Load(total));
				return Node::Success(sum);
			}

			const HIRNode* Constant(int64 value) { return m_Ctx.out->NewNode<ConstantNode>(Int64(), ConstantValue::Int(value)); }

			const HIRNode* Convert(const HIRNode* count)
			{
				return count->type == Int64() ? count : m_Ctx.out->NewNode<ConvertNode>(count, count->type, Int64());
			}

			const Type* Int64() const { return m_Types->Builtin(TypeKind::Int64); }

		private:
			Node OperatorCall(const ObjectType* object, const ObjectType::MethodInfo& method, const LValue* location, CallDispatch dispatch)
			{
				const Type* returned = TypeHandle::UnwrapToValue(method.m_ReturnType);
				const FunctionRecord* rec = m_Ctx.queries().RecordForMethod(object, &method);
				if (method.m_IsStatic || !method.m_ParameterTypes.Empty() || !returned || !returned->IsIntegral() || !rec)
				{
					const SourceLocation at = method.m_DeclaringNode ? method.m_DeclaringNode->startLocation : SourceLocation();
					return Node::Fail(QueryError::NotAValue, "[operator(\"native_bytes\")] of " + QualifiedName(object) +
						" must be a method that takes nothing and returns int64", at);
				}
				auto* call = m_Ctx.out->NewNode<CallNode>(method.m_ReturnType, rec);
				if (auto bound = BindReceiver(m_Ctx, call, location, dispatch); !bound.Ok()) return Node::Fail(bound.error, bound.message);
				return ImplicitConversion::Scalar(m_Ctx, call, Int64());
			}

			// A type without the operator: its bases' and fields' native bytes.
			Node Parts(const ObjectType* object, const LValue* location)
			{
				const HIRNode* total = nullptr;
				for (uint32 i = 0; i < object->GetBaseTypes().Count(); ++i)
				{
					const ObjectType* base = object->GetBaseTypes()[i];
					if (!NativeMemory::Holds(base)) continue;
					Node part = At(SubobjectLValue(m_Ctx, location, base, static_cast<int64>(object->GetBaseOffset(i))), CallDispatch::Direct);
					if (!part.Ok()) return part;
					total = total ? Add(total, part.value) : part.value;
				}
				for (const ObjectType::FieldInfo& field : object->GetFields())
				{
					if (!NativeMemory::Counted(field)) continue;
					LValue lv; lv.kind = LValue::Kind::Field; lv.type = field.m_Type; lv.base = location; lv.field = &field;
					Node part = At(m_Ctx.NewLValue(lv), CallDispatch::Direct);
					if (!part.Ok()) return part;
					total = total ? Add(total, part.value) : part.value;
				}
				return Node::Success(total ? total : Constant(0));
			}

			const HIRNode* FirstElement(const LValue* array, const ArrayType* type)
			{
				return m_Ctx.out->NewNode<AddressOfNode>(m_Types->Pointer(type->GetElementType()), array);
			}

			const LValue* Local(BlockNode* block, const Type* type, const HIRNode* init, const char* name)
			{
				const uint32 slot = m_Ctx.AllocSlot(type, name, SlotClass::Temp);
				block->stmts.push_back(m_Ctx.out->NewNode<LocalDeclNode>(slot, type, init));
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = type; lv.slot = slot;
				return m_Ctx.NewLValue(lv);
			}

			const HIRNode* Load(const LValue* location) { return m_Ctx.out->NewNode<LoadNode>(location->type, location); }

			const HIRNode* Add(const HIRNode* a, const HIRNode* b) { return m_Ctx.out->NewNode<IntrinsicOpNode>(Int64(), IntrinsicOp::Add, a, b); }

			LoweringContext& m_Ctx;
			TypeStore* m_Types;
		};

		// The location a reference names, or `location` itself.
		const LValue* Referent(LoweringContext& ctx, const LValue* location)
		{
			if (!location->type || location->type->GetKind() != TypeKind::Reference) return location;
			LValue lv; lv.kind = LValue::Kind::Deref; lv.type = TypeHandle::UnwrapToValue(location->type);
			lv.addr = ctx.out->NewNode<LoadNode>(location->type, location);
			return ctx.NewLValue(lv);
		}

		Result OfValue(LoweringContext& ctx, const Value& value)
		{
			if (!value.isLValue) return Fail("native_bytes_of(x) counts x where it is: a variable, field, element or *p");
			std::vector<const HIRNode*> steps;
			const LValue* location = Referent(ctx, EvaluatedOnce(ctx, value.lvalue, steps));
			Summer summer(ctx);
			Node sum = NativeMemory::Holds(location->type) ? summer.At(location, CallDispatch::Dynamic) : Node::Success(summer.Constant(0));
			if (!sum.Ok()) return Result::Fail(sum.error, sum.message, sum.loc);
			return Value::R(AfterSteps(ctx, steps, sum.value), summer.Int64());
		}

		Result OfElements(LoweringContext& ctx, const Value& pointer, const Value& count)
		{
			const HIRNode* first = pointer.isLValue || pointer.rvalue ? ctx.LoadIfNeeded(pointer) : nullptr;
			const Type* t = first ? TypeHandle::UnwrapToValue(first->type) : nullptr;
			const Type* element = t && t->GetKind() == TypeKind::Pointer ? TypeHandle::UnwrapToValue(static_cast<const PointerType*>(t)->GetPointeeType()) : nullptr;
			if (!element || element->GetKind() == TypeKind::Void) return Fail("native_bytes_of(p, count) takes a pointer to the first element (not void*)");
			Summer summer(ctx);
			auto n = count.isLValue || count.rvalue ? ImplicitConversion::Scalar(ctx, ctx.LoadIfNeeded(count), summer.Int64()) : Node::Fail(QueryError::NotAValue, "");
			if (!n.Ok()) return Fail("native_bytes_of(p, count) takes the count of elements as an integer");
			if (NativeMemory::Holds(element))
			{
				Node sum = summer.Elements(first, n.value, element);
				if (!sum.Ok()) return Result::Fail(sum.error, sum.message, sum.loc);
				return Value::R(sum.value, summer.Int64());
			}
			auto* evaluated = ctx.out->NewNode<SequenceNode>(summer.Int64());
			evaluated->steps = { first, n.value, summer.Constant(0) };
			return Value::R(evaluated, summer.Int64());
		}

		// The object the census function's parameter points to, as a `type`.
		const LValue* ObjectAt(LoweringContext& ctx, const Type* type, uint64 offset)
		{
			TypeStore* types = ctx.queries().TypeStore();
			const Type* voidPtr = types->Pointer(types->Builtin(TypeKind::Void));
			LValue param; param.kind = LValue::Kind::Slot; param.type = voidPtr; param.slot = 0;
			const HIRNode* address = ctx.out->NewNode<LoadNode>(voidPtr, ctx.NewLValue(param));
			if (offset)
				address = ctx.out->NewNode<AddressArithmeticNode>(voidPtr, address,
					ctx.out->NewNode<ConstantNode>(types->Builtin(TypeKind::Int64), ConstantValue::Int(static_cast<int64>(offset))));
			LValue lv; lv.kind = LValue::Kind::Deref; lv.type = type;
			lv.addr = ctx.out->NewNode<ConvertNode>(address, voidPtr, types->Pointer(type));
			return ctx.NewLValue(lv);
		}

		Node CensusSum(LoweringContext& ctx, const Type* type, uint64 countBytes)
		{
			Summer summer(ctx);
			if (!countBytes) return summer.At(ObjectAt(ctx, type, 0), CallDispatch::Direct);
			TypeStore* types = ctx.queries().TypeStore();
			const HIRNode* count = ctx.out->NewNode<LoadNode>(summer.Int64(), ObjectAt(ctx, summer.Int64(), 0));
			const HIRNode* first = ctx.out->NewNode<AddressOfNode>(types->Pointer(type), ObjectAt(ctx, type, countBytes));
			return summer.Elements(first, count, type);
		}
	}

	bool IsIntrinsic(const Core::String& name) { return name == "native_bytes_of"; }

	QueryResult<Value> LowerIntrinsic(LoweringContext& ctx, const std::vector<Value>& args)
	{
		if (args.size() == 1) return OfValue(ctx, args[0]);
		if (args.size() == 2) return OfElements(ctx, args[0], args[1]);
		return Fail("native_bytes_of takes a variable, field or element (x), or a pointer and a count (p, count)");
	}

	HIRFunction LowerCensusFunction(ProgramModel& model, ErrorTracker& errors, const Type* type, uint64 countBytes, const FunctionRecord& function)
	{
		HIRFunction fn;
		fn.record = &function;
		LoweringContext ctx(&model, &errors);
		ctx.fn = &function;
		ctx.out = &fn;
		ScopeGuard frame = ctx.queries().PushScope(Scope::Kind::Function, function.mangledName);
		fn.root = fn.NewNode<BlockNode>(nullptr);
		TypeStore* types = model.TypeStorePtr();
		ctx.AllocSlot(types->Pointer(types->Builtin(TypeKind::Void)), "object", SlotClass::Param);
		Node sum = CensusSum(ctx, type, countBytes);
		if (sum.Ok()) fn.root->stmts.push_back(fn.NewNode<ReturnNode>(sum.value));
		else ctx.Fail(nullptr, sum);
		fn.complete = sum.Ok() && !errors.HasErrors();
		return fn;
	}
} // namespace Scripting::NativeBytes
