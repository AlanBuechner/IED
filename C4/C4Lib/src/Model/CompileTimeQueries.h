#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Lexer/Token.h"
#include "Core/Core.h"

namespace Scripting
{
	class Type;
	struct TypeOfExpressionNode;

	/// The compile-time queries on a type (docs/language/10 4): its size, its alignment and the offset of a
	/// field path. A reference measures what it refers to; an incomplete type or `void` is an error.
	QueryResult<int64> SizeOfType(const Type* type);
	QueryResult<int64> AlignOfType(const Type* type);
	QueryResult<int64> OffsetOfField(const Type* type, const Core::Vector<Token>& path);

	/// The value of `sizeof` / `alignof` / `offsetof` (an int64) or `holds_native_bytes` (a bool) once its operand's
	/// type is known.
	QueryResult<ConstantValue> CompileTimeQueryValue(const TypeOfExpressionNode& query, const Type* operandType);
}
