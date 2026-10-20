#pragma once
#include "Model/QueryResult.h"
#include "Model/Types/TypeKind.h"
#include "Core/Core.h"
#include <string_view>

namespace Scripting
{
	/// A numeric literal's type and value (docs/language/03 3.6-3.7): an integer's bits (an unsigned value
	/// above INT64_MAX keeps its bit pattern) or a floating-point value.
	struct NumericLiteral
	{
		TypeKind type = TypeKind::Int32;
		uint64 bits = 0;
		double real = 0.0;

		bool IsFloat() const { return type == TypeKind::Float32 || type == TypeKind::Float64; }
	};

	/// The type and value of a numeric literal's spelling (one the lexer accepted), or an error when the
	/// value does not fit any type its suffix allows. Lowering and constant folding both read literals here.
	QueryResult<NumericLiteral> ParseNumericLiteral(std::string_view spelling);

	/// `char8`, `char16` or `char32` for a character or string literal's code-unit width.
	TypeKind CharacterType(uint8 unitWidth);
	/// A character literal's value: its one code unit, sign-extended for the signed `char8` (as C's char).
	int64 CharacterValue(const Core::String& units, uint8 unitWidth);
}
