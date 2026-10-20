#include "Backend/HIR/NewArray.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/DeclLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/AccessCheck.h"
#include "Backend/HIR/IndexLoop.h"
#include "Backend/HIR/IndexLValue.h"
#include "Model/TypeHandle.h"
#include "Parser/Nodes/Expressions/NewExpressionNode.h"
#include <algorithm>
#include <limits>

namespace Scripting
{
	namespace
	{
		// The prelude's thrower for a count that cannot hold the array (Bedrock/Prelude/prelude.ch).
		constexpr const char* kBadLength = "__c4_bad_array_length";

		// How diagnostics name the array being started.
		Core::String Described(const Type* element) { return "new " + element->GetName() + "[n]"; }
	}

	QueryResult<Value> NewArrayLowerer::Lower(const NewExpressionNode* n, const Type* element, const HIRNode* placement)
	{
		// count = n; the length check; then the allocation, which starts the elements before it yields them.
		auto count = Count(n);
		if (!count.Ok()) return QueryResult<Value>::Fail(count.error, count.message);
		auto named = ElementsNamed(n, element);
		if (!named.Ok()) return QueryResult<Value>::Fail(named.error, named.message);
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* pointer = ts->Pointer(element);
		const LValue* countSlot = NewSlot(ts->Builtin(TypeKind::Int64), "__count");
		const LValue* elements = NewSlot(pointer, "__elements");
		auto check = LengthCheck(n, countSlot, named.value, element);
		if (!check.Ok()) return QueryResult<Value>::Fail(check.error, check.message);
		auto start = Initialize(n, element, elements, countSlot, named.value);
		if (!start.Ok()) return QueryResult<Value>::Fail(start.error, start.message);

		auto* allocation = m_Ctx.out->NewNode<NewNode>(pointer, element);
		allocation->placement = placement;
		allocation->count = LoadOf(countSlot);
		allocation->elements = elements;
		allocation->initialize = start.value;
		if (!placement) allocation->finalizer = Finalizer(element);
		auto* sequence = m_Ctx.out->NewNode<SequenceNode>(pointer);
		sequence->steps = { m_Ctx.out->NewNode<StoreNode>(countSlot, count.value), check.value, allocation };
		return Value::R(sequence, pointer);
	}

	QueryResult<const HIRNode*> NewArrayLowerer::Count(const NewExpressionNode* n)
	{
		using Result = QueryResult<const HIRNode*>;
		auto value = m_Expr.ResolveExpr(n->GetArrayCount().Get());
		if (!value.Ok()) return Result::Fail(value.error, value.message);
		const HIRNode* count = m_Ctx.LoadIfNeeded(value.value);
		const Type* given = TypeHandle::UnwrapToValue(count->type);
		if (!given || !given->IsIntegral() || given->IsNormalized())
			return m_Ctx.Fail(n->GetArrayCount().Get(), Result::Fail(QueryError::NotAValue, "the element count of 'new T[n]' is an integer, got '" +
				(given ? given->GetName() : Core::String("?")) + "'"));
		const Type* i64 = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		return Result(given == i64 ? count : m_Ctx.out->NewNode<ConvertNode>(count, given, i64));
	}

	QueryResult<uint64> NewArrayLowerer::ElementsNamed(const NewExpressionNode* n, const Type* element)
	{
		if (!n->GetArrayInitializer()) return QueryResult<uint64>(0);
		return m_Ctx.Fail(n, DeclLowerer(m_Ctx).ElementsNamed(n->GetArrayInitializer().Get(), Described(element)));
	}

	QueryResult<BlockNode*> NewArrayLowerer::LengthCheck(const NewExpressionNode* n, const LValue* count, uint64 minimum, const Type* element)
	{
		// if (count < minimum || count > the most elements a 64-bit size holds) __c4_bad_array_length(count, minimum)
		using Result = QueryResult<BlockNode*>;
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* i64 = ts->Builtin(TypeKind::Int64);
		const FunctionRecord* thrower = nullptr;
		auto found = m_Ctx.queries().QueryFunction(kBadLength, nullptr, nullptr, Core::Vector<const Type*>{ i64, i64 }, m_Ctx.unitId,
			m_Ctx.enclosingType, m_Ctx.scope, &thrower);
		if (!found.Ok() || !thrower)
			return m_Ctx.Fail(n, Result::Fail(QueryError::NotFound, "'new T[n]' throws the prelude's BadArrayLength, and this program has "
				"no prelude: the default library (Bedrock) is missing"));
		const int64 most = std::numeric_limits<int64>::max() / static_cast<int64>(std::max<size_t>(element->GetSize(), 1));
		auto* check = m_Ctx.out->NewNode<BlockNode>(nullptr);
		auto failWhen = [&](IntrinsicOp op, int64 bound) {
			auto* call = m_Ctx.out->NewNode<CallNode>(found.value->GetReturnType(), thrower);
			call->args = { LoadOf(count), Int64(static_cast<int64>(minimum)) };
			auto* test = m_Ctx.out->NewNode<IfNode>(m_Ctx.out->NewNode<IntrinsicOpNode>(ts->Builtin(TypeKind::Bool), op, LoadOf(count), Int64(bound)));
			test->thenBlock = m_Ctx.out->NewNode<BlockNode>(nullptr);
			test->thenBlock->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(call));
			check->stmts.push_back(test);
		};
		failWhen(IntrinsicOp::Lt, static_cast<int64>(minimum));
		failWhen(IntrinsicOp::Gt, most);
		return Result(check);
	}

	QueryResult<BlockNode*> NewArrayLowerer::Initialize(const NewExpressionNode* n, const Type* element, const LValue* elements,
		const LValue* count, uint64 named)
	{
		auto* start = m_Ctx.out->NewNode<BlockNode>(nullptr);
		const LValue* started = nullptr;
		BlockNode* body = start;
		if (LifecycleFacts::NeedsDestroy(m_Ctx.queries(), element))
		{
			started = NewSlot(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), "__started");
			start->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(started, Int64(0)));
			body = StartedGuard(start, element, elements, started);
		}
		if (auto r = StartNamed(n, element, elements, named, body); !r.Ok()) return QueryResult<BlockNode*>::Fail(r.error, r.message);
		if (started) body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(started, Int64(static_cast<int64>(named))));
		if (auto r = StartRest(n, element, elements, count, started, named, body); !r.Ok()) return QueryResult<BlockNode*>::Fail(r.error, r.message);
		return QueryResult<BlockNode*>(start);
	}

	QueryResult<void> NewArrayLowerer::StartNamed(const NewExpressionNode* n, const Type* element, const LValue* elements, uint64 named,
		BlockNode* region)
	{
		// The elements the braces name are a `T[named]` at the start of the array, filled as a declaration's would be.
		if (named == 0) return QueryResult<void>::Success();
		TypeStore* ts = m_Ctx.queries().TypeStore();
		const Type* leading = ts->Array(element, named);
		LValue lv;
		lv.kind = LValue::Kind::Deref;
		lv.type = leading;
		lv.addr = m_Ctx.out->NewNode<ConvertNode>(LoadOf(elements), elements->type, ts->Pointer(leading));
		return DeclLowerer(m_Ctx).StartFromBraces(n->GetArrayInitializer().Get(), m_Ctx.NewLValue(lv), region, n, Described(element));
	}

	QueryResult<void> NewArrayLowerer::StartRest(const NewExpressionNode* n, const Type* element, const LValue* elements, const LValue* count,
		const LValue* started, uint64 named, BlockNode* region)
	{
		if (!LifecycleFacts::NeedsConstruct(m_Ctx.queries(), element)) return QueryResult<void>::Success();
		if (auto allowed = AccessCheck::CheckDefaultCreate(m_Ctx, element); !allowed.Ok()) return m_Ctx.Fail(n, allowed);
		const Type* i64 = m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64);
		Lifecycle life(m_Ctx);
		IndexRange range{ [&] { return Int64(static_cast<int64>(named)); }, [&] { return LoadOf(count); } };
		AppendIndexLoop(m_Ctx, region, range, false, [&](BlockNode* body, const HIRNode* index) {
			life.Construct(body, ElementAt(elements, element, index));
			if (started)
				body->stmts.push_back(m_Ctx.out->NewNode<StoreNode>(started, m_Ctx.out->NewNode<IntrinsicOpNode>(i64, IntrinsicOp::Add, LoadOf(started), Int64(1))));
		});
		return QueryResult<void>::Success();
	}

	BlockNode* NewArrayLowerer::StartedGuard(BlockNode* region, const Type* element, const LValue* elements, const LValue* started)
	{
		Lifecycle life(m_Ctx);
		return life.GuardWith(region, [&](BlockNode* undo) {
			IndexRange range{ [&] { return Int64(0); }, [&] { return LoadOf(started); } };
			AppendIndexLoop(m_Ctx, undo, range, true, [&](BlockNode* body, const HIRNode* index) {
				life.DestroyNow(body, ElementAt(elements, element, index));
			});
		});
	}

	const FunctionRecord* NewArrayLowerer::Finalizer(const Type* element)
	{
		// A `T[2][n]` destroys every T: the innermost element's delete runs on each of them.
		if (!LifecycleFacts::NeedsDestroy(m_Ctx.queries(), element)) return nullptr;
		const Type* innermost = TypeHandle::UnwrapToValue(element);
		while (const auto* array = dynamic_cast<const ArrayType*>(innermost)) innermost = TypeHandle::UnwrapToValue(array->GetElementType());
		const auto* object = dynamic_cast<const ObjectType*>(innermost);
		return object ? Lifecycle(m_Ctx).Destroy(object) : nullptr;
	}

	const LValue* NewArrayLowerer::ElementAt(const LValue* elements, const Type* element, const HIRNode* index)
	{
		return BuildIndexLValue(m_Ctx, Value::L(elements), index, element, element->GetSize());
	}

	const LValue* NewArrayLowerer::NewSlot(const Type* type, const char* name)
	{
		LValue lv;
		lv.kind = LValue::Kind::Slot;
		lv.type = type;
		lv.slot = m_Ctx.AllocSlot(type, name, SlotClass::Temp);
		return m_Ctx.NewLValue(lv);
	}

	const HIRNode* NewArrayLowerer::Int64(int64 value)
	{
		return m_Ctx.out->NewNode<ConstantNode>(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int(value));
	}

	const HIRNode* NewArrayLowerer::LoadOf(const LValue* location)
	{
		return m_Ctx.out->NewNode<LoadNode>(location->type, location);
	}
} // namespace Scripting
