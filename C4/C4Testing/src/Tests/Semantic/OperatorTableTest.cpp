#include "OperatorTableTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/OperatorTable.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	using Scripting::OperatorTable;

	namespace
	{
		const Scripting::ProgramModel& MakeModel(Scripting::CompilerInstance& compiler, const char* source)
		{
			uint32 id = compiler.RegisterInMemory("t.cf", source);
			compiler.CompileAll();
			compiler.AnalyzeProgram();
			return compiler.GetCompilationUnit(id)->GetProgramModel();
		}
	}

	void OperatorTableTest::ArityPicksTheMeaning()
	{
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("-", 0) == "negate");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("-", 1) == "subtract");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("*", 0) == "dereference");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("*", 1) == "multiply");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("&", 0) == "address_of");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("&", 1) == "bitwise_and");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("+", 0) == "unary_plus");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("+", 1) == "add");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("!", 0) == "logical_not");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("++", 0) == "increment");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("[]", 1) == "index");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("[]=", 2) == "index_assign");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("subtract", 0) == "subtract"); // a name is strict
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("hash", 0) == "hash");
		TEST_PASSED();
	}

	void OperatorTableTest::CompletedNames()
	{
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("()", 0) == "call");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("()", 3) == "call");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("<<=", 1) == "shift_left_assign");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation(">>=", 1) == "shift_right_assign");
		TEST_ASSERT(OperatorTable::CanonicalAnnotation("**", 1) == "power");
		TEST_ASSERT(OperatorTable::PlainFormFor("shift_left_assign") == "shift_left");
		TEST_ASSERT(OperatorTable::PlainFormFor("and_assign") == "bitwise_and");
		TEST_ASSERT(OperatorTable::IsAssignForm("shift_right_assign"));
		TEST_ASSERT(!OperatorTable::IsAssignForm("power"));
		TEST_PASSED();
	}

	void OperatorTableTest::EveryNameRegisters()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct Flags { int32 bits;\n"
			"  [operator(\"less\", \"greater\")] public bool Differs(const Flags& o) const { return bits != o.bits; }\n"
			"  [operator(\"-\")] public Flags Flip() const { return *this; }\n"
			"  [operator(\"-\")] public Flags Minus(const Flags& o) const { return o; } }\n");
		auto flags = model.QueryType("Flags");
		TEST_ASSERT(flags.Ok());
		auto less = model.QueryOperator("<", flags.value, flags.value);
		auto greater = model.QueryOperator(">", flags.value, flags.value);
		TEST_ASSERT(less.Ok() && greater.Ok());
		TEST_ASSERT(less.value.record == greater.value.record);
		auto negate = model.QueryOperator("-", flags.value, nullptr);
		auto subtract = model.QueryOperator("-", flags.value, flags.value);
		TEST_ASSERT(negate.Ok() && negate.value.method->mName == "Flip");
		TEST_ASSERT(subtract.Ok() && subtract.value.method->mName == "Minus");
		TEST_PASSED();
	}

	void OperatorTableTest::PrimitiveExtensionsAreReceivers()
	{
		Scripting::CompilerInstance compiler;
		const auto& model = MakeModel(compiler,
			"struct V { float x; }\n"
			"[operator(\"-\")] V Minus(const V& v) extends float { return v; }\n");
		auto v = model.QueryType("V");
		auto f = model.QueryType("float");
		TEST_ASSERT(v.Ok() && f.Ok());
		auto scalarLeft = model.QueryOperator("-", f.value, v.value);
		TEST_ASSERT(scalarLeft.Ok());
		TEST_ASSERT(scalarLeft.value.record != nullptr && scalarLeft.value.method == nullptr);
		auto bothScalar = model.QueryOperator("-", f.value, f.value);
		TEST_ASSERT(bothScalar.error == Scripting::QueryError::NotFound); // the extension cannot take a float
		auto objectLeft = model.QueryOperator("-", v.value, f.value);
		TEST_ASSERT(objectLeft.error == Scripting::QueryError::NotFound); // operands never swap
		TEST_PASSED();
	}
} // namespace Testing
