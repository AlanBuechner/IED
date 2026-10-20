#include "Backend/HIR/MethodBody.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/StmtLowerer.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include "Parser/Nodes/Statements/ExpressionStatementNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Model/TypeHandle.h"

namespace Scripting
{
	namespace
	{
		enum class MethodRole { Plain, Create, Delete, Copy };

		bool Annotated(const FunctionRecord* rec, const char* name)
		{
			for (const Core::String& a : rec->operatorAnnotations)
				if (a == name) return true;
			return false;
		}

		MethodRole RoleOf(const FunctionRecord* rec)
		{
			if (!rec->containingType || rec->isStatic) return MethodRole::Plain;
			if (Annotated(rec, "create")) return MethodRole::Create;
			if (Annotated(rec, "delete")) return MethodRole::Delete;
			if (Annotated(rec, "copy")) return MethodRole::Copy;
			return MethodRole::Plain;
		}

		// `Base::Name(...);` as the body's first statement, where Name is a method of the class base that
		// carries `annotation`: the body constructs (or copies) the base itself.
		bool FirstStatementCallsBase(LoweringContext& ctx, const CodeBlockNode* body, const ObjectType* base, const char* annotation)
		{
			if (!base || body->GetStatements().Empty()) return false;
			const auto* stmt = dynamic_cast<const ExpressionStatementNode*>(body->GetStatements()[0].Get());
			const auto* call = stmt ? dynamic_cast<const FunctionCallNode*>(stmt->GetExpression().Get()) : nullptr;
			const auto* qualified = call ? dynamic_cast<const StaticAccessNode*>(call->GetCallee().Get()) : nullptr;
			const auto* owner = qualified ? dynamic_cast<const IdentifierExpressionNode*>(qualified->GetBaseType().Get()) : nullptr;
			if (!owner || owner->GetName() != base->GetName()) return false;
			const Core::String method = qualified->GetMemberNameToken().GetText();
			for (uint32 i = 0; i < base->GetMethodCount(); ++i)
			{
				if (base->GetMethods()[i].mName != method) continue;
				const FunctionRecord* rec = ctx.queries().RecordForMethod(base, &base->GetMethods()[i]);
				if (rec && Annotated(rec, annotation)) return true;
			}
			return false;
		}

		void ScheduleParameterDestruction(LoweringContext& ctx, Lifecycle& life, const FunctionRecord* record, BlockNode* root)
		{
			for (uint32 i = 0; record->type && i < record->type->GetParameterCount(); ++i)
			{
				const Type* p = record->type->GetParameters()[i].m_Type;
				if (!p || p->GetKind() == TypeKind::Reference) continue;
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = TypeHandle::UnwrapToValue(p); lv.slot = i;
				ctx.NoteCleanup(i, life.ScheduleDestroy(root, ctx.NewLValue(lv)), root);
			}
		}

		// A create starts the whole object: each reference field without an initializer is bound in its body.
		QueryResult<void> BindsEveryReference(LoweringContext& ctx, const FunctionRecord* record, const CodeBlockNode* body)
		{
			const ObjectType::FieldInfo* unbound = ReferenceBinding::FirstUnbound(record->containingType, ctx.boundReferences);
			if (!unbound) return QueryResult<void>::Success();
			return ctx.Fail(body, QueryResult<void>::Fail(QueryError::NotAValue, "the create '" + record->name + "' of '" +
				record->containingType->GetName() + "' does not bind its reference field '" + unbound->mName +
				"': assign it a location in the create, or give it an initializer"));
		}

		const LValue* CopySource(LoweringContext& ctx, const FunctionRecord* record)
		{
			const Type* refT = record->type->GetParameters()[0].m_Type;
			LValue slot; slot.kind = LValue::Kind::Slot; slot.slot = 0; slot.type = refT;
			LValue deref; deref.kind = LValue::Kind::Deref; deref.type = record->containingType;
			deref.addr = ctx.out->NewNode<LoadNode>(ctx.queries().TypeStore()->Pointer(record->containingType, true), ctx.NewLValue(slot));
			return ctx.NewLValue(deref);
		}
	}

	QueryResult<void> LowerFunctionBody(LoweringContext& ctx, const FunctionRecord* record, const CodeBlockNode* body, BlockNode* root)
	{
		Lifecycle life(ctx);
		StmtLowerer stmts(ctx);
		if (record->synthesized != LifecycleOp::None)
		{
			RegionScope scope(ctx, root, "lifecycle");
			return life.LowerSynthesized(record, root);
		}
		ScheduleParameterDestruction(ctx, life, record, root);

		const ObjectType* type = record->containingType;
		const ObjectType* base = type ? LifecycleFacts::ClassBase(type) : nullptr;
		switch (RoleOf(record))
		{
		case MethodRole::Create:
		{
			bool explicitBase = FirstStatementCallsBase(ctx, body, base, "create");
			BlockNode* building = record->isNoexcept ? root : life.PartsGuard(root, type);
			auto lowered = stmts.LowerFunctionBody(body, building, explicitBase ? 1 : 0,
				[&]() { return life.ConstructParts(building, type, explicitBase, body); });
			if (!lowered.Ok()) return lowered;
			return BindsEveryReference(ctx, record, body);
		}
		case MethodRole::Delete:
			life.ScheduleDestroyParts(root, type);
			return stmts.LowerFunctionBody(body, root, 0, nullptr);
		case MethodRole::Copy:
		{
			bool explicitBase = FirstStatementCallsBase(ctx, body, base, "copy");
			BlockNode* building = record->isNoexcept ? root : life.PartsGuard(root, type);
			if (!explicitBase && record->type && record->type->GetParameterCount() == 1) life.CopyBase(building, type, CopySource(ctx, record));
			return stmts.LowerFunctionBody(body, building, 0, nullptr);
		}
		default:
			return stmts.LowerFunctionBody(body, root, 0, nullptr);
		}
	}
} // namespace Scripting
