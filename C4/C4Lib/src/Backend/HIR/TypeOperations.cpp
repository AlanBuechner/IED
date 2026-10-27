#include "Backend/HIR/TypeOperations.h"
#include "Backend/HIR/Construction.h"
#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/Lifecycle.h"
#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/LoweringContext.h"
#include "Model/Abstractness.h"
#include "Model/ProgramModel.h"
#include "Model/TypeHandle.h"
#include "Model/Types/ArrayType.h"
#include "Model/Types/FunctionType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/UnionType.h"
#include "ReflectABI.h"
#include <unordered_set>

namespace Scripting
{
	namespace
	{
		using Visited = std::unordered_set<const Type*>;

		const ObjectType* AsObject(const Type* type)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			return value && value->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(value) : nullptr;
		}

		// A class-template instance's copy, written in its template, compiles for the instance's arguments only when they
		// can be copied themselves (a container copies its elements): the copy is taken as needing that of each type
		// argument. Every other copy is the program's own code, or made of its parts'.
		bool ArgumentsCopy(const ModelQueries& q, const ObjectType* instance, Visited& visited);

		bool PartsCopy(const ModelQueries& q, const Type* type, Visited& visited)
		{
			const Type* value = TypeHandle::UnwrapToValue(type);
			if (!value || !visited.insert(value).second) return true;
			if (value->GetKind() == TypeKind::Array) return PartsCopy(q, static_cast<const ArrayType*>(value)->GetElementType(), visited);
			const ObjectType* object = AsObject(value);
			if (!object) return true;
			if (!ArgumentsCopy(q, object, visited)) return false;
			if (const ObjectType* base = LifecycleFacts::ClassBase(object); base && !PartsCopy(q, base, visited)) return false;
			bool copies = true;
			LifecycleFacts::ForEachField(object, [&](const ObjectType::FieldInfo& field) { copies = copies && PartsCopy(q, field.m_Type, visited); });
			return copies;
		}

		bool ArgumentsCopy(const ModelQueries& q, const ObjectType* instance, Visited& visited)
		{
			const LifecycleFacts::OwnMethods& own = LifecycleFacts::Own(q, instance);
			const FunctionRecord* copy = own.copy ? own.copy : own.assign;
			if (!copy || copy->templateOrigin != TemplateOrigin::ClassMember) return true;
			for (const ObjectType::TemplateArgument& argument : instance->GetTemplateArguments())
			{
				if (!argument.type) continue;
				if (!LifecycleFacts::CheckCopyable(q, argument.type).Ok() || !PartsCopy(q, argument.type, visited)) return false;
			}
			return true;
		}

		bool Copies(const ModelQueries& q, const ObjectType* type)
		{
			if (!LifecycleFacts::CheckCopyable(q, type).Ok()) return false;
			Visited visited;
			return PartsCopy(q, type, visited);
		}

		// A type that declares creates but no default one (each takes arguments) has no start from nothing.
		bool StartsFromNothing(const ModelQueries& q, const ObjectType* type)
		{
			const LifecycleFacts::OwnMethods& own = LifecycleFacts::Own(q, type);
			return own.defaultCreate || own.creates.empty();
		}

		TypeOperations ObjectOperations(ProgramModel& model, const ObjectType* type)
		{
			const ModelQueries q(&model);
			TypeOperations ops;
			if (TypeHandle::MayHoldPointers(type)) ops.flags |= reflect_TYPE_HOLDS_POINTERS;
			if (!Abstractness::Creation(model, type).empty())
			{
				ops.flags |= reflect_TYPE_ABSTRACT;
				return ops;
			}
			if (StartsFromNothing(q, type))
			{
				ops.flags |= reflect_TYPE_DEFAULT_CONSTRUCTIBLE;
				ops.construct = LifecycleFacts::NeedsConstruct(q, type) || type->IsPolymorphic();
			}
			ops.destroy = LifecycleFacts::NeedsDestroy(q, type);
			if (Copies(q, type))
			{
				ops.flags |= reflect_TYPE_COPYABLE;
				ops.copy = LifecycleFacts::NeedsCopy(q, type);
			}
			return ops;
		}

		const char* SymbolPrefix(TypeOperation op)
		{
			switch (op)
			{
			case TypeOperation::Construct: return "__c4_reflect_construct_";
			case TypeOperation::Destroy: return "__c4_reflect_destroy_";
			case TypeOperation::Copy: return "__c4_reflect_copy_";
			}
			return "__c4_reflect_construct_";
		}

		// The thunk's parameters: slot 0 the object (or the copy's target), slot 1 the copy's source.
		enum ThunkSlot : uint32 { kObject = 0, kSource = 1 };

		class TypeThunkBuilder
		{
		public:
			TypeThunkBuilder(LoweringContext& ctx, const ObjectType& type) : m_Ctx(ctx), m_Type(type), m_Types(ctx.queries().TypeStore()) {}

			QueryResult<void> Build(TypeOperation op)
			{
				const Type* voidPtr = m_Types->Pointer(m_Types->Builtin(TypeKind::Void));
				m_Ctx.AllocSlot(voidPtr, op == TypeOperation::Copy ? "to" : "object", SlotClass::Param);
				if (op == TypeOperation::Copy) m_Ctx.AllocSlot(voidPtr, "from", SlotClass::Param);
				BlockNode* root = m_Ctx.out->root;
				return Lifecycle(m_Ctx).LowerStatement(root, [&]() -> QueryResult<void>
					{
						if (op == TypeOperation::Destroy)
						{
							Lifecycle(m_Ctx).DestroyNow(root, Object(kObject));
							return QueryResult<void>::Success();
						}
						std::vector<ConstructionArgument> args;
						if (op == TypeOperation::Copy) args.push_back({ Value::L(Object(kSource)) });
						auto made = ExprLowerer(m_Ctx).PlacementNew(nullptr, &m_Type, Address(kObject), args);
						if (!made.Ok()) return QueryResult<void>::Fail(made.error, made.message);
						root->stmts.push_back(m_Ctx.out->NewNode<ExprStmtNode>(m_Ctx.LoadIfNeeded(made.value)));
						return QueryResult<void>::Success();
					});
			}

		private:
			// The slot's address as a T*.
			const HIRNode* Address(ThunkSlot slot)
			{
				const Type* voidPtr = m_Types->Pointer(m_Types->Builtin(TypeKind::Void));
				LValue lv; lv.kind = LValue::Kind::Slot; lv.type = voidPtr; lv.slot = slot;
				const HIRNode* raw = m_Ctx.out->NewNode<LoadNode>(voidPtr, m_Ctx.NewLValue(lv));
				return m_Ctx.out->NewNode<ConvertNode>(raw, voidPtr, m_Types->Pointer(&m_Type));
			}

			// `*(T*)slot`.
			const LValue* Object(ThunkSlot slot)
			{
				LValue lv; lv.kind = LValue::Kind::Deref; lv.type = &m_Type; lv.addr = Address(slot);
				return m_Ctx.NewLValue(lv);
			}

			LoweringContext& m_Ctx;
			const ObjectType& m_Type;
			TypeStore* m_Types;
		};
	}

	TypeOperations TypeOperationsOf(ProgramModel& model, const Type* type)
	{
		if (const ObjectType* object = AsObject(type)) return ObjectOperations(model, object);
		TypeOperations ops;
		ops.flags = PlainTypeFlags(type);
		return ops;
	}

	uint32 PlainTypeFlags(const Type* type)
	{
		const uint32 flags = reflect_TYPE_DEFAULT_CONSTRUCTIBLE | reflect_TYPE_COPYABLE;
		return TypeHandle::MayHoldPointers(type) ? flags | reflect_TYPE_HOLDS_POINTERS : flags;
	}

	std::string TypeOperationSymbol(const Type* type, TypeOperation op)
	{
		return std::string(SymbolPrefix(op)) + EscapedText(QualifiedName(type)).c_str();
	}

	const FunctionType* TypeOperationThunkType(ProgramModel& model, TypeOperation op)
	{
		TypeStore* types = model.TypeStorePtr();
		const Type* voidPtr = types->Pointer(types->Builtin(TypeKind::Void));
		if (op == TypeOperation::Copy) return types->Function(types->Builtin(TypeKind::Void), { voidPtr, voidPtr });
		return types->Function(types->Builtin(TypeKind::Void), { voidPtr });
	}

	HIRFunction LowerTypeOperationThunk(ProgramModel& model, ErrorTracker& errors, const ObjectType& type, TypeOperation op, const FunctionRecord& thunk)
	{
		HIRFunction fn;
		fn.record = &thunk;
		LoweringContext ctx(&model, &errors);
		ctx.fn = &thunk;
		ctx.out = &fn;
		ctx.unitId = 0; // the type's own code, which sees what the type sees
		ctx.enclosingType = &type;
		ctx.scope = model.TypeScope(&type);
		ScopeCursor cursor(*model.Symbols()); // thunks lower on several threads (ParallelLowering)
		ScopeGuard frame = ctx.queries().PushScope(Scope::Kind::Function, thunk.mangledName);
		fn.root = fn.NewNode<BlockNode>(nullptr);
		fn.complete = TypeThunkBuilder(ctx, type).Build(op).Ok() && errors.GetErrors().Empty();
		return fn;
	}
} // namespace Scripting
