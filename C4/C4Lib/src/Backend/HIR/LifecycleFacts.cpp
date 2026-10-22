#include "Backend/HIR/LifecycleFacts.h"
#include "Backend/HIR/ReferenceBinding.h"
#include "Model/TypeHandle.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"

namespace Scripting
{
	namespace LifecycleFacts
	{
		namespace
		{
			// `Init() {}`: nothing to run beyond the zeroing every object already gets.
			bool IsEmptyCreate(const FunctionRecord* rec)
			{
				const auto* body = dynamic_cast<const CodeBlockNode*>(rec->bodyNode);
				return body && body->GetStatements().Empty() && !rec->isLinked && rec->type && rec->type->GetParameterCount() == 0;
			}

			// A copy-like method takes exactly one reference (or value) of its own type.
			bool TakesOwnType(const FunctionRecord* rec, const ObjectType* type)
			{
				if (!rec->type || rec->type->GetParameterCount() != 1) return false;
				return TypeHandle::UnwrapToValue(rec->type->GetParameters()[0].m_Type) == type;
			}

			const ObjectType* AsObject(const Type* t)
			{
				t = TypeHandle::UnwrapToValue(t);
				return t && t->GetKind() == TypeKind::Object ? static_cast<const ObjectType*>(t) : nullptr;
			}

			const Type* ElementOf(const Type* t)
			{
				t = TypeHandle::UnwrapToValue(t);
				return t && t->GetKind() == TypeKind::Array ? static_cast<const ArrayType*>(t)->GetElementType() : nullptr;
			}

			// True when `own` or any part of the object (its class base, its fields) has the property. A pure packed
			// vector has none: it copies and goes away as one register value (its delete and copy are errors).
			bool AnyPart(const ModelQueries& q, const Type* t, bool (*own)(const OwnMethods&))
			{
				if (ReferenceBinding::Of(t)) return false; // a reference owns nothing
				if (const Type* elem = ElementOf(t)) return AnyPart(q, elem, own);
				const ObjectType* obj = AsObject(t);
				if (!obj || obj->IsPurePackedVector()) return false;
				if (own(Own(q, obj))) return true;
				if (const ObjectType* base = ClassBase(obj); base && AnyPart(q, base, own)) return true;
				bool found = false;
				ForEachField(obj, [&](const ObjectType::FieldInfo& f) { found = found || AnyPart(q, f.m_Type, own); });
				return found;
			}
		}

		const OwnMethods& Own(const ModelQueries& q, const ObjectType* type)
		{
			return q.Model()->LifecycleMethodsOf(type);
		}

		const ObjectType* ClassBase(const ObjectType* type)
		{
			for (const ObjectType* base : type->GetBaseTypes())
				if (base && !base->IsInterface()) return base;
			return nullptr;
		}

		void ForEachField(const ObjectType* type, const std::function<void(const ObjectType::FieldInfo&)>& visit)
		{
			for (const auto& f : type->GetFields())
				if (!f.m_IsStatic && !f.m_InUnionRegion && f.m_Type) visit(f);
		}

		bool NeedsConstruct(const ModelQueries& q, const Type* t)
		{
			// A default create with something to run, or a field initializer, anywhere in the object.
			if (ReferenceBinding::Of(t)) return false;
			if (const Type* elem = ElementOf(t)) return NeedsConstruct(q, elem);
			const ObjectType* obj = AsObject(t);
			if (!obj) return false;
			if (const FunctionRecord* create = Own(q, obj).defaultCreate; create && !IsEmptyCreate(create)) return true;
			if (const ObjectType* base = ClassBase(obj); base && NeedsConstruct(q, base)) return true;
			bool found = false;
			ForEachField(obj, [&](const ObjectType::FieldInfo& f) { found = found || f.m_Initializer || NeedsConstruct(q, f.m_Type); });
			return found;
		}

		bool NeedsDestroy(const ModelQueries& q, const Type* t)
		{
			return AnyPart(q, t, [](const OwnMethods& o) { return o.destroy != nullptr; });
		}

		bool NeedsCopy(const ModelQueries& q, const Type* t)
		{
			return AnyPart(q, t, [](const OwnMethods& o) { return o.copy != nullptr || o.assign != nullptr; });
		}

		QueryResult<void> CheckCopyable(const ModelQueries& q, const Type* t)
		{
			if (ReferenceBinding::Of(t)) return QueryResult<void>::Success(); // copying a reference copies the address
			if (const Type* elem = ElementOf(t)) return CheckCopyable(q, elem);
			const ObjectType* obj = AsObject(t);
			if (!obj) return QueryResult<void>::Success();
			OwnMethods own = Own(q, obj);
			if (own.copy || own.assign) return QueryResult<void>::Success();
			if (own.destroy)
				return QueryResult<void>::Fail(QueryError::NotAValue, "'" + obj->GetName() + "' cannot be copied: it has [operator(\"delete\")] "
					"but no [operator(\"copy\")]; add a copy operator, or move the value with take(x)");
			if (const ObjectType* base = ClassBase(obj))
				if (auto r = CheckCopyable(q, base); !r.Ok()) return r;
			QueryResult<void> result = QueryResult<void>::Success();
			ForEachField(obj, [&](const ObjectType::FieldInfo& f) { if (result.Ok()) result = CheckCopyable(q, f.m_Type); });
			if (!result.Ok())
				return QueryResult<void>::Fail(result.error, "'" + obj->GetName() + "' cannot be copied memberwise: " + result.message);
			return result;
		}
	}
} // namespace Scripting
