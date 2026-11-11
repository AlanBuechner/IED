#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Lexer/Token.h"
#include "Core/Core.h"
#include <optional>

namespace Scripting
{
	class Type;
	class ProgramModel;
	class Scope;
	struct TypeOfExpressionNode;

	/// The compile-time queries on a type (docs/language/10 4): its size, its alignment and the offset of a
	/// field path. A reference measures what it refers to; an incomplete type or `void` is an error.
	QueryResult<int64> SizeOfType(const Type* type);
	QueryResult<int64> AlignOfType(const Type* type);
	QueryResult<int64> OffsetOfField(const Type* type, const Core::Vector<Token>& path);

	/// The value of `sizeof` / `alignof` / `offsetof` (an int64) or `holds_native_bytes` / `holds_pointers` (a bool) once
	/// its operand's type is known.
	QueryResult<ConstantValue> CompileTimeQueryValue(const TypeOfExpressionNode& query, const Type* operandType);

	/// `sizeof("text")` measures the literal's array, its terminator too, as in C++; nothing for any other query.
	std::optional<int64> SizeOfStringLiteral(const TypeOfExpressionNode& query);

	/// Whether `query` is a call of a program function of its name (`holds_pointers` declared by the program), which
	/// wins over the query where it is visible.
	bool CallsProgramFunction(const ProgramModel& model, const TypeOfExpressionNode& query, uint32 unitId, const Scope* scope);
}
