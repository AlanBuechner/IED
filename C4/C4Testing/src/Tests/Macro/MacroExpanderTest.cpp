#include "MacroExpanderTest.h"
#include "Macro/MacroExpander.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		std::string Expand(const MacroTable& table, const std::string& text)
		{
			const ExpansionResult result = MacroExpander::Expand(text, table, { "dir/t.cf", 7 });
			return result.errors.empty() ? result.text : "error: " + result.errors.front();
		}
	}

	void MacroExpanderTest::ParametersSubstituteAtOnce()
	{
		MacroTable table;
		table.DefineFunction("F", { "a", "b" }, "a + b");
		table.DefineFunction("SWAP", { "x", "y" }, "y, x");
		TEST_ASSERT(Expand(table, "F(b, 1)") == "b + 1");      // BUG-125: the `b` that replaced `a` stays
		TEST_ASSERT(Expand(table, "SWAP(y, x)") == "x, y");
		TEST_ASSERT(Expand(table, "F( (1, 2) , 3 )") == "(1, 2) + 3");
		TEST_ASSERT(Expand(table, "F(ab, bc)") == "ab + bc"); // whole names only
	}

	void MacroExpanderTest::LiteralsAndCommentsAreKept()
	{
		MacroTable table;
		table.Define("X", "1");
		table.DefineFunction("S", { "x" }, "\"x\" 'x' x");
		TEST_ASSERT(Expand(table, "S(2)") == "\"x\" 'x' 2");                 // BUG-125: not inside literals
		TEST_ASSERT(Expand(table, "\"X\" X // X") == "\"X\" 1 // X");
		TEST_ASSERT(Expand(table, "/* X */ X") == "/* X */ 1");
		TEST_ASSERT(Expand(table, "u8\"X\" u\"X\" U'X'") == "u8\"X\" u\"X\" U'X'");
	}

	void MacroExpanderTest::StringizeQuotesTheArgumentAsWritten()
	{
		MacroTable table;
		table.Define("ONE", "1");
		table.DefineFunction("STR", { "x" }, "#x");
		table.DefineFunction("XSTR", { "x" }, "STR(x)");
		TEST_ASSERT(Expand(table, "STR(ONE)") == "\"ONE\"");          // not expanded first
		TEST_ASSERT(Expand(table, "XSTR(ONE)") == "\"1\"");           // the classic two-level form
		TEST_ASSERT(Expand(table, "STR(  a   +  b )") == "\"a + b\""); // spaces collapse
		TEST_ASSERT(Expand(table, "STR(\"q\\n\")") == "\"\\\"q\\\\n\\\"\""); // quotes and backslashes escaped
		TEST_ASSERT(Expand(table, "STR()") == "\"\"");
	}

	void MacroExpanderTest::PasteJoinsTokens()
	{
		MacroTable table;
		table.Define("ONE", "1");
		table.DefineFunction("CAT", { "a", "b" }, "a ## b");
		table.DefineFunction("CAT3", { "a", "b", "c" }, "a##b##c");
		table.Define("AB", "x ## y");
		TEST_ASSERT(Expand(table, "CAT(my, Var)") == "myVar");
		TEST_ASSERT(Expand(table, "CAT(1, 2)") == "12");
		TEST_ASSERT(Expand(table, "CAT(ONE, 2)") == "ONE2");            // operands of ## are not expanded
		TEST_ASSERT(Expand(table, "CAT3(a, b, c)") == "abc");
		TEST_ASSERT(Expand(table, "AB") == "xy");
		TEST_ASSERT(Expand(table, "CAT(, x)") == "x");
	}

	void MacroExpanderTest::ResultsAreRescannedWithoutRecursion()
	{
		MacroTable table;
		table.Define("A", "B + 1");
		table.Define("B", "A * 2");
		table.Define("SELF", "SELF + 1");
		table.DefineFunction("ID", { "x" }, "x");
		table.Define("CALL", "ID");
		TEST_ASSERT(Expand(table, "A") == "A * 2 + 1");
		TEST_ASSERT(Expand(table, "SELF") == "SELF + 1");
		TEST_ASSERT(Expand(table, "ID(ID(3))") == "3");                 // arguments expand before substitution
		TEST_ASSERT(Expand(table, "CALL(4)") == "4");                   // an alias of a function-like macro
		TEST_ASSERT(Expand(table, "ID") == "ID");                       // a function-like name with no call
	}

	void MacroExpanderTest::ArgumentCountsAndVariadics()
	{
		MacroTable table;
		table.DefineFunction("Z", {}, "7");
		table.DefineFunction("ONE", { "x" }, "[x]");
		table.DefineFunction("TWO", { "a", "b" }, "a b");
		table.DefineFunction("V", { "..." }, "f(__VA_ARGS__)");
		table.DefineFunction("NV", { "first", "..." }, "first: __VA_ARGS__");
		TEST_ASSERT(Expand(table, "Z()") == "7");
		TEST_ASSERT(Expand(table, "ONE()") == "[]");
		TEST_ASSERT(Expand(table, "V(1, 2, 3)") == "f(1, 2, 3)");
		TEST_ASSERT(Expand(table, "V(9,)") == "f(9)");
		TEST_ASSERT(Expand(table, "NV(a, b, c)") == "a: b, c");
		TEST_ASSERT(Expand(table, "NV(a)") == "a: ");
		TEST_ASSERT(Expand(table, "Z(1)") == "error: the macro 'Z' takes 0 arguments, but 1 was given");
		TEST_ASSERT(Expand(table, "TWO(1)") == "error: the macro 'TWO' takes 2 arguments, but 1 was given");
		TEST_ASSERT(Expand(table, "TWO(1, 2, 3)") == "error: the macro 'TWO' takes 2 arguments, but 3 were given");
		TEST_ASSERT(Expand(table, "NV()") == ": ");
	}

	void MacroExpanderTest::FileAndLine()
	{
		MacroTable table;
		TEST_ASSERT(Expand(table, "__FILE__ __LINE__") == "\"dir/t.cf\" 7");
		table.Define("HERE", "__LINE__");
		TEST_ASSERT(Expand(table, "HERE") == "7");
	}

	void MacroExpanderTest::CallsThatContinueOnTheNextLine()
	{
		MacroTable table;
		table.DefineFunction("ADD", { "a", "b" }, "((a) + (b))");
		TEST_ASSERT(MacroExpander::EndsInsideCall("int32 x = ADD(1,", table));
		TEST_ASSERT(MacroExpander::EndsInsideCall("ADD(f(1), (2", table));
		TEST_ASSERT(!MacroExpander::EndsInsideCall("int32 x = ADD(1, 2);", table));
		TEST_ASSERT(!MacroExpander::EndsInsideCall("f(1,", table));          // not a macro
		TEST_ASSERT(!MacroExpander::EndsInsideCall("\"ADD(1,\"", table));    // inside a literal
		TEST_ASSERT(!MacroExpander::EndsInsideCall("// ADD(1,", table));     // inside a comment
	}
} // namespace Testing
