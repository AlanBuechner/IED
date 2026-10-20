#include "MacroExpressionTest.h"
#include "Macro/MacroExpression.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		MacroTable Table()
		{
			MacroTable table;
			table.Define("ZERO", "0");
			table.Define("TWO", "2");
			table.Define("EMPTY", "");
			table.Define("SUM", "TWO + 1");
			table.DefineFunction("TWICE", { "x" }, "((x) * 2)");
			return table;
		}

		int64 Value(const char* condition)
		{
			const ConditionValue value = EvaluateCondition(condition, Table(), { "t.cf", 1 });
			return value.Ok() ? value.value : -999999;
		}

		std::string ErrorOf(const char* condition)
		{
			return EvaluateCondition(condition, Table(), { "t.cf", 1 }).error;
		}
	}

	void MacroExpressionTest::EveryOperator()
	{
		const struct { const char* condition; int64 value; } cases[] = {
			{ "1", 1 }, { "0", 0 }, { "!0", 1 }, { "!5", 0 }, { "~0", -1 }, { "-3", -3 }, { "+3", 3 },
			{ "7 * 3", 21 }, { "7 / 2", 3 }, { "7 % 4", 3 }, { "5 + 4", 9 }, { "5 - 9", -4 },
			{ "1 << 4", 16 }, { "-16 >> 2", -4 }, { "2 < 3", 1 }, { "3 <= 3", 1 }, { "2 > 3", 0 }, { "3 >= 4", 0 },
			{ "2 == 2", 1 }, { "2 != 2", 0 }, { "6 & 3", 2 }, { "6 ^ 3", 5 }, { "6 | 3", 7 },
			{ "1 && 0", 0 }, { "1 && 2", 1 }, { "0 || 0", 0 }, { "0 || 7", 1 },
			{ "1 ? 10 : 20", 10 }, { "0 ? 10 : 20", 20 }, { "(1 + 2) * 3", 9 },
		};
		for (const auto& c : cases)
			TEST_ASSERT_MSG(Value(c.condition) == c.value, "{0}", c.condition);
	}

	void MacroExpressionTest::PrecedenceAndAssociativity()
	{
		TEST_ASSERT(Value("1 + 2 * 3") == 7);
		TEST_ASSERT(Value("1 << 2 + 1") == 8);
		TEST_ASSERT(Value("1 < 2 == 1") == 1);
		TEST_ASSERT(Value("6 & 3 == 3") == 0);       // == binds tighter than &
		TEST_ASSERT(Value("1 | 2 ^ 3 & 4") == 3);
		TEST_ASSERT(Value("0 && 1 || 1") == 1);
		TEST_ASSERT(Value("1 || 0 && 0") == 1);
		TEST_ASSERT(Value("10 - 4 - 3") == 3);         // left associative
		TEST_ASSERT(Value("100 / 10 / 5") == 2);
		TEST_ASSERT(Value("0 ? 1 : 0 ? 2 : 3") == 3); // ?: is right associative
		TEST_ASSERT(Value("-2 * 3") == -6);
		TEST_ASSERT(Value("!0 + 1") == 2);
	}

	void MacroExpressionTest::DefinedAndMacroValues()
	{
		TEST_ASSERT(Value("defined(TWO)") == 1);
		TEST_ASSERT(Value("defined TWO") == 1);
		TEST_ASSERT(Value("defined ( ZERO )") == 1);
		TEST_ASSERT(Value("defined(NOPE)") == 0);
		TEST_ASSERT(Value("!defined NOPE") == 1);
		TEST_ASSERT(Value("defined(TWO) && defined(ZERO)") == 1);
		TEST_ASSERT(Value("defined(TWO) && defined(NOPE)") == 0);
		TEST_ASSERT(Value("defined(NOPE) || defined(TWO)") == 1);
		TEST_ASSERT(Value("ZERO") == 0);               // `#define X 0` then `#if X` is false
		TEST_ASSERT(Value("TWO == 2") == 1);
		TEST_ASSERT(Value("SUM") == 3);                // expanded recursively
		TEST_ASSERT(Value("TWICE(TWO) == 4") == 1);
		TEST_ASSERT(Value("NOPE") == 0);               // a name left after expansion is 0
		TEST_ASSERT(Value("NOPE + 5") == 5);
		TEST_ASSERT(Value("defined(__FILE__) && defined __LINE__") == 1);
		TEST_ASSERT(Value("__LINE__ == 1") == 1);
		TEST_ASSERT(Value("true") == 1);
		TEST_ASSERT(Value("false || 1") == 1);
	}

	void MacroExpressionTest::LiteralsInConditions()
	{
		TEST_ASSERT(Value("0x10 == 16") == 1);
		TEST_ASSERT(Value("0b101 == 5") == 1);
		TEST_ASSERT(Value("010 == 8") == 1);
		TEST_ASSERT(Value("10u == 10") == 1);
		TEST_ASSERT(Value("'A' == 65") == 1);
		TEST_ASSERT(Value("'\\n' == 10") == 1);
		TEST_ASSERT(Value("0xFFFFFFFFFFFFFFFF == -1") == 1); // the bits of a uint64 read as int64
	}

	void MacroExpressionTest::SkippedOperandsAreNotEvaluated()
	{
		TEST_ASSERT(Value("0 && 1 / 0") == 0);
		TEST_ASSERT(Value("1 || 1 / 0") == 1);
		TEST_ASSERT(Value("1 ? 2 : 1 / 0") == 2);
		TEST_ASSERT(Value("0 ? 1 % 0 : 3") == 3);
		TEST_ASSERT(Value("0 && (1 << 99)") == 0);
	}

	void MacroExpressionTest::MalformedConditionsAreErrors()
	{
		TEST_ASSERT(ErrorOf("") == "a #if condition is empty");
		TEST_ASSERT(ErrorOf("EMPTY") == "a #if condition is empty");
		TEST_ASSERT(ErrorOf("1 / 0") == "division by zero in a #if condition");
		TEST_ASSERT(ErrorOf("1 % ZERO") == "remainder by zero in a #if condition");
		TEST_ASSERT(ErrorOf("1 << 64").find("out of range") != std::string::npos);
		TEST_ASSERT(ErrorOf("(1 + 2") == "a '(' in a #if condition has no ')'");
		TEST_ASSERT(ErrorOf("1 +") == "a #if condition ends early: an operand is missing");
		TEST_ASSERT(ErrorOf("1 ? 2") == "a '?' in a #if condition needs its ':'");
		TEST_ASSERT(ErrorOf("1 2") == "unexpected '2' in a #if condition");
		TEST_ASSERT(ErrorOf("1 )") == "unexpected ')' in a #if condition");
		TEST_ASSERT(ErrorOf("1.5") == "a #if condition takes integers, but '1.5' is a floating-point number");
		TEST_ASSERT(ErrorOf("10q").find("'10q' has the invalid suffix 'q'") == 0);
		TEST_ASSERT(ErrorOf("defined") == "'defined' needs a macro name");
		TEST_ASSERT(ErrorOf("defined(X") == "'defined(' needs a macro name and a ')'");
		TEST_ASSERT(ErrorOf("1 $ 2") == "unexpected '$' in a #if condition");
		TEST_ASSERT(ErrorOf("TWICE(1, 2)").find("the macro 'TWICE' takes 1 argument") == 0);
	}
} // namespace Testing
