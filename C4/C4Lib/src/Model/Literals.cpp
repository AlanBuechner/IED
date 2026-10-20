#include "Model/Literals.h"
#include "Lexer/LiteralScanner.h"
#include <cfloat>
#include <charconv>
#include <string>
#include <vector>

namespace Scripting
{
	namespace
	{
		bool HasUnsigned(std::string_view suffix) { return suffix.find_first_of("uU") != std::string_view::npos; }
		bool HasLong(std::string_view suffix) { return suffix.find_first_of("lL") != std::string_view::npos; }

		// The types a literal may take, in order: the first that holds the value is its type (C's rule).
		std::vector<TypeKind> IntegerCandidates(const NumberSpelling& spelling)
		{
			const bool isUnsigned = HasUnsigned(spelling.suffix);
			const bool isLong = HasLong(spelling.suffix);
			if (isUnsigned && isLong) return { TypeKind::UInt64 };
			if (isUnsigned) return { TypeKind::UInt32, TypeKind::UInt64 };
			if (isLong) return { TypeKind::Int64, TypeKind::UInt64 };
			if (spelling.radix == 10) return { TypeKind::Int32, TypeKind::Int64, TypeKind::UInt64 };
			return { TypeKind::Int32, TypeKind::UInt32, TypeKind::Int64, TypeKind::UInt64 };
		}

		bool Holds(TypeKind type, uint64 value)
		{
			switch (type)
			{
			case TypeKind::Int32: return value <= 0x7FFFFFFFull;
			case TypeKind::UInt32: return value <= 0xFFFFFFFFull;
			case TypeKind::Int64: return value <= 0x7FFFFFFFFFFFFFFFull;
			default: return true;
			}
		}

		QueryResult<NumericLiteral> Integer(std::string_view spelling, const NumberSpelling& parts)
		{
			uint64 value = 0;
			const char* end = parts.digits.data() + parts.digits.size();
			const auto parsed = std::from_chars(parts.digits.data(), end, value, static_cast<int>(parts.radix));
			if (parsed.ec == std::errc::result_out_of_range)
				return QueryResult<NumericLiteral>::Fail(QueryError::NotAValue,
					"integer literal '" + std::string(spelling) + "' is too large: the largest is 18446744073709551615 (0xFFFFFFFFFFFFFFFF)");
			if (parsed.ec != std::errc() || parsed.ptr != end)
				return QueryResult<NumericLiteral>::Fail(QueryError::NotAValue, "'" + std::string(spelling) + "' is not a number");
			for (TypeKind type : IntegerCandidates(parts))
				if (Holds(type, value))
					return QueryResult<NumericLiteral>::Success({ type, value, 0.0 });
			return QueryResult<NumericLiteral>::Fail(QueryError::NotAValue,
				"integer literal '" + std::string(spelling) + "' does not fit any type its suffix allows");
		}

		// FLT_MAX plus half its last place: a float literal at or past it rounds to infinity; one below rounds to at most
		// FLT_MAX, as `3.40282347e+38f` (how FLT_MAX prints) does (BUG-287).
		constexpr double kFloatOverflow = 3.4028235677973366e+38;

		QueryResult<NumericLiteral> Float(std::string_view spelling, const NumberSpelling& parts)
		{
			double value = 0.0;
			const char* end = parts.digits.data() + parts.digits.size();
			const auto parsed = std::from_chars(parts.digits.data(), end, value);
			const bool isSingle = parts.suffix == "f" || parts.suffix == "F";
			const bool outOfRange = parsed.ec == std::errc::result_out_of_range || (isSingle && value >= kFloatOverflow);
			if (outOfRange)
				return QueryResult<NumericLiteral>::Fail(QueryError::NotAValue,
					"floating-point literal '" + std::string(spelling) + "' is out of range for " + (isSingle ? "float" : "double"));
			if (parsed.ec != std::errc() || parsed.ptr != end)
				return QueryResult<NumericLiteral>::Fail(QueryError::NotAValue, "'" + std::string(spelling) + "' is not a number");
			return QueryResult<NumericLiteral>::Success({ isSingle ? TypeKind::Float32 : TypeKind::Float64, 0, value });
		}
	}

	QueryResult<NumericLiteral> ParseNumericLiteral(std::string_view spelling)
	{
		const NumberSpelling parts = SplitNumber(spelling);
		return parts.isFloat ? Float(spelling, parts) : Integer(spelling, parts);
	}

	TypeKind CharacterType(uint8 unitWidth)
	{
		return unitWidth == 4 ? TypeKind::Char32 : unitWidth == 2 ? TypeKind::Char16 : TypeKind::Char8;
	}

	int64 CharacterValue(const Core::String& units, uint8 unitWidth)
	{
		if (CodeUnitCount(units, unitWidth) == 0) return 0;
		const uint32 unit = CodeUnitAt(units, unitWidth, 0);
		return unitWidth == 1 ? static_cast<int64>(static_cast<int8>(unit)) : static_cast<int64>(unit);
	}
}
