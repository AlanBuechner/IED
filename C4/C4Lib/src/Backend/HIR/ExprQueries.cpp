#include "Backend/HIR/ExprLowerer.h"
#include "Backend/HIR/ReadOnly.h"
#include "Model/CompileTimeQueries.h"
#include "Model/Templates/Packs.h"
#include "Model/Types.h"
#include "Model/TypeHandle.h"
#include "Model/Types/NameGuid.h"
#include "Model/Types/QualifiedName.h"
#include <array>
#include "Parser/Nodes/Expressions/TypeOfExpressionNode.h"

namespace Scripting
{
	// Compile-time queries (docs/language/10 4): sizeof, alignof, offsetof (int64 constants), __uuidof (the
	// address of a COM GUID), nameof (a type's qualified name) and sizeof...(pack). Nothing runs: an expression operand
	// is typed, not evaluated.

	QueryResult<Value> ExprLowerer::ResolveTypeOf(const TypeOfExpressionNode* n)
	{
		if (n->GetKind() == TypeOfExpressionNode::Kind::SizeOfPack) return ResolveSizeOfPack(n);
		if (n->GetKind() == TypeOfExpressionNode::Kind::UuidOf) return ResolveUuidOf(n);
		if (n->GetKind() == TypeOfExpressionNode::Kind::NameOf) return ResolveNameOf(n);
		auto operand = QueryOperandType(n);
		if (!operand.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(operand.error, operand.message));
		auto value = CompileTimeQueryValue(*n, operand.value);
		if (!value.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(value.error, value.message));
		return Constant(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int(value.value));
	}

	// The type the query measures: the type operand when it names a type, else the expression operand's type
	// (`sizeof(x)` parses as both; a name that is not a type is a value).
	QueryResult<const Type*> ExprLowerer::QueryOperandType(const TypeOfExpressionNode* n)
	{
		if (n->GetTargetType())
		{
			auto type = m_Ctx.queries().ResolveTypeCtx(*n->GetTargetType().Get(), m_Ctx.unitId, m_Ctx.scope);
			if (type.Ok() || !n->GetOperand()) return type;
		}
		return UnevaluatedType(n->GetOperand().Get());
	}

	// The operand lowers into a scratch tree that is thrown away, so it emits nothing and leaves no temporaries.
	QueryResult<const Type*> ExprLowerer::UnevaluatedType(const AstNode* operand)
	{
		HIRFunction scratch;
		HIRFunction* body = m_Ctx.out;
		const size_t temporaries = m_Ctx.pendingTemporaries.size();
		const size_t transfers = m_Ctx.transfers.size();
		m_Ctx.out = &scratch;
		auto value = ResolveExpr(operand);
		m_Ctx.out = body;
		m_Ctx.pendingTemporaries.resize(temporaries);
		m_Ctx.transfers.resize(transfers);
		if (!value.Ok()) return QueryResult<const Type*>::Fail(value.error, value.message);
		if (value.value.isLValue && IsThisObject(value.value.lvalue)) // `sizeof(this)` is a pointer's size, as in C++
			return QueryResult<const Type*>::Success(AddressType(m_Ctx, value.value.lvalue));
		return QueryResult<const Type*>::Success(value.value.type);
	}

	QueryResult<Value> ExprLowerer::ResolveSizeOfPack(const TypeOfExpressionNode* n)
	{
		const Core::String& name = n->GetPackName().GetText();
		const SymbolInfo* pack = Packs::Find(m_Ctx.model(), name, nullptr);
		if (!pack)
			return m_Ctx.Fail(n, QueryResult<Value>::Fail(QueryError::NotFound, "sizeof...(" + name + "): '" + name + "' is not a pack"));
		return Constant(m_Ctx.queries().TypeStore()->Builtin(TypeKind::Int64), ConstantValue::Int(static_cast<int64>(pack->packElements->size())));
	}

	namespace
	{
		std::array<uint8_t, 16> WrittenGuidBytes(const ObjectType::GuidParts& g)
		{
			std::array<uint8_t, 16> bytes{};
			for (int i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((g.m_Data1 >> (8 * i)) & 0xff); // little-endian
			for (int i = 0; i < 2; ++i) bytes[4 + i] = static_cast<uint8_t>((g.m_Data2 >> (8 * i)) & 0xff);
			for (int i = 0; i < 2; ++i) bytes[6 + i] = static_cast<uint8_t>((g.m_Data3 >> (8 * i)) & 0xff);
			for (int i = 0; i < 8; ++i) bytes[8 + i] = g.m_Data4[i];
			return bytes;
		}
	}

	// nameof(T): the type's qualified name as reflection registers it (D-50) - `Core::Array<int32>`, `Geo::Point*` -
	// a string literal; for an expression, its type's.
	QueryResult<Value> ExprLowerer::ResolveNameOf(const TypeOfExpressionNode* n)
	{
		auto operand = QueryOperandType(n);
		if (!operand.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(operand.error, "nameof: " + operand.message));
		TypeStore* types = m_Ctx.queries().TypeStore();
		return Constant(types->Pointer(types->Builtin(TypeKind::Char8)), ConstantValue::String(QualifiedName(operand.value), 1));
	}

	// __uuidof(T): the [uuid("...")] a type carries (CBind emits it for __declspec(uuid) / MIDL_INTERFACE), else the
	// name-based UUID of "c4:" + its qualified name (D-43). The value is void*, converting to `_GUID*` / `const GUID*`.
	QueryResult<Value> ExprLowerer::ResolveUuidOf(const TypeOfExpressionNode* n)
	{
		auto t = m_Ctx.queries().ResolveTypeCtx(*n->GetTargetType().Get(), m_Ctx.unitId, m_Ctx.scope);
		if (!t.Ok()) return m_Ctx.Fail(n, QueryResult<Value>::Fail(t.error, "__uuidof: " + t.message));
		const auto* obj = dynamic_cast<const ObjectType*>(TypeHandle::UnwrapToValue(t.value));
		const std::array<uint8_t, 16> bytes = obj && obj->HasGuid() ? WrittenGuidBytes(obj->GetGuid())
			: NameGuid::GuidBytes(std::string("c4:") + QualifiedName(t.value).c_str());
		TypeStore* types = m_Ctx.queries().TypeStore();
		return Constant(types->Pointer(types->Builtin(TypeKind::Void)), ConstantValue::Guid(bytes.data()));
	}
}
