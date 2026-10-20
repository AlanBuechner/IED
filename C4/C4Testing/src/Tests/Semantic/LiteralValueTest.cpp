#include "LiteralValueTest.h"
#include "Model/Literals.h"

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		bool Typed(const char* spelling, TypeKind type)
		{
			auto literal = ParseNumericLiteral(spelling);
			return literal.Ok() && literal.value.type == type;
		}

		bool Valued(const char* spelling, uint64 bits)
		{
			auto literal = ParseNumericLiteral(spelling);
			return literal.Ok() && !literal.value.IsFloat() && literal.value.bits == bits;
		}
	}

	void LiteralValueTest::IntegersTakeTheFirstTypeThatFits()
	{
		const struct { const char* spelling; TypeKind type; } cases[] = {
			{ "0", TypeKind::Int32 }, { "2147483647", TypeKind::Int32 }, { "2147483648", TypeKind::Int64 },
			{ "9223372036854775807", TypeKind::Int64 }, { "9223372036854775808", TypeKind::UInt64 },
			{ "0x7FFFFFFF", TypeKind::Int32 }, { "0x80000000", TypeKind::UInt32 }, { "0xFFFFFFFF", TypeKind::UInt32 },
			{ "0x100000000", TypeKind::Int64 }, { "0xFFFFFFFFFFFFFFFF", TypeKind::UInt64 },
			{ "037777777777", TypeKind::UInt32 }, { "0b10000000000000000000000000000000", TypeKind::UInt32 },
			{ "1u", TypeKind::UInt32 }, { "0xFFFFFFFFu", TypeKind::UInt32 }, { "4294967296u", TypeKind::UInt64 },
			{ "1l", TypeKind::Int64 }, { "1LL", TypeKind::Int64 }, { "0xFFFFFFFFFFFFFFFFll", TypeKind::UInt64 },
			{ "1ul", TypeKind::UInt64 }, { "1LLU", TypeKind::UInt64 }, { "1lu", TypeKind::UInt64 },
		};
		for (const auto& c : cases)
			TEST_ASSERT_MSG(Typed(c.spelling, c.type), "{0}", c.spelling);
	}

	void LiteralValueTest::IntegerValuesAndLimits()
	{
		TEST_ASSERT(Valued("010", 8));
		TEST_ASSERT(Valued("0", 0));
		TEST_ASSERT(Valued("0x1F", 31));
		TEST_ASSERT(Valued("0b101", 5));
		TEST_ASSERT(Valued("18446744073709551615", 0xFFFFFFFFFFFFFFFFull));
		TEST_ASSERT(Valued("0xFFFFFFFFFFFFFFFF", 0xFFFFFFFFFFFFFFFFull));
		auto tooLarge = ParseNumericLiteral("18446744073709551616");
		TEST_ASSERT(!tooLarge.Ok() && tooLarge.message.find("integer literal '18446744073709551616' is too large") == 0);
		TEST_ASSERT(!ParseNumericLiteral("0x10000000000000000").Ok());
		TEST_ASSERT(!ParseNumericLiteral("0b11111111111111111111111111111111111111111111111111111111111111111").Ok());
	}

	void LiteralValueTest::FloatTypesAndRange()
	{
		TEST_ASSERT(Typed("1.5", TypeKind::Float64));
		TEST_ASSERT(Typed("1e3", TypeKind::Float64));
		TEST_ASSERT(Typed("1.5f", TypeKind::Float32));
		TEST_ASSERT(Typed("2.5E-3F", TypeKind::Float32));
		auto value = ParseNumericLiteral("1e3");
		TEST_ASSERT(value.Ok() && value.value.real == 1000.0);
		auto single = ParseNumericLiteral("1e39f");
		TEST_ASSERT(!single.Ok() && single.message == "floating-point literal '1e39f' is out of range for float");
		TEST_ASSERT(ParseNumericLiteral("1e39").Ok());
		TEST_ASSERT(!ParseNumericLiteral("1e999").Ok());
	}

	void LiteralValueTest::CharacterValues()
	{
		TEST_ASSERT(CharacterType(1) == TypeKind::Char8);
		TEST_ASSERT(CharacterType(2) == TypeKind::Char16);
		TEST_ASSERT(CharacterType(4) == TypeKind::Char32);
		TEST_ASSERT(CharacterValue(Core::String("A"), 1) == 65);
		TEST_ASSERT(CharacterValue(Core::String("\xFF"), 1) == -1);              // char8 is signed
		TEST_ASSERT(CharacterValue(Core::String("\xFF\xFF", 2), 2) == 0xFFFF);   // char16 is unsigned
		TEST_ASSERT(CharacterValue(Core::String("\x00\xF6\x01\x00", 4), 4) == 0x1F600);
		TEST_ASSERT(CharacterValue(Core::String(), 1) == 0);
	}
} // namespace Testing
