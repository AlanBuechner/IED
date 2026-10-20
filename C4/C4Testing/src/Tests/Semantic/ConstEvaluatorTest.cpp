#include "ConstEvaluatorTest.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ConstEvaluator.h"
#include "Model/ProgramModel.h"
#include <string>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		// One program's model and its constants by name.
		class Constants
		{
		public:
			explicit Constants(const char* source)
			{
				m_Compiler.RegisterInMemory("consts.cf", source);
				m_Compiler.CompileAll();
				m_Compiler.AnalyzeProgram();
			}

			QueryResult<ConstantValue> Of(const char* name) { return ConstEvaluator(m_Compiler.GetProgramModel()).EvaluateName(name); }
			std::string Diagnostics()
			{
				std::string all;
				for (const auto& e : m_Compiler.GetAllDiagnostics()) all += std::string(e.message.c_str()) + "\n";
				return all;
			}
			int64 Int(const char* name)
			{
				const QueryResult<ConstantValue> value = Of(name);
				return value.Ok() ? value.value.intValue : INT64_MIN;
			}

		private:
			CompilerInstance m_Compiler;
		};

		bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
	}

	void ConstEvaluatorTest::ConstantsFoldThroughEachOtherInAnyOrder()
	{
		Constants program(
			"const int32 Area = Width * Height;\n"
			"const int32 Width = Height + 2;\n"
			"const int32 Height = 3;\n"
			"namespace Limits { const int64 Max = Area * 10; }\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("Height"), int64(3));
		TEST_ASSERT_EQ(program.Int("Width"), int64(5));
		TEST_ASSERT_EQ(program.Int("Area"), int64(15));
		TEST_ASSERT_EQ(program.Int("Limits::Max"), int64(150));
		TEST_PASSED();
	}

	// BUG-204: an initializer naming an earlier member (`Ten = Six + 4`) left the whole enum unresolved.
	void ConstEvaluatorTest::EnumMembersCountOnFromTheLastValue()
	{
		Constants program(
			"enum class Code : int32 { Zero, One, Five = 5, Six, Ten = Six + 4, Eleven }\n"
			"const int32 FromEnum = (int32)Code::Ten * 2;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("Code::Zero"), int64(0));
		TEST_ASSERT_EQ(program.Int("Code::One"), int64(1));
		TEST_ASSERT_EQ(program.Int("Code::Six"), int64(6));
		TEST_ASSERT_EQ(program.Int("Code::Ten"), int64(10));
		TEST_ASSERT_EQ(program.Int("Code::Eleven"), int64(11));
		TEST_ASSERT_EQ(program.Int("FromEnum"), int64(20));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::IntegerOperatorsFoldAsInC()
	{
		Constants program(
			"const int32 Sum = 7 + 5;\n"
			"const int32 Difference = 7 - 12;\n"
			"const int32 Quotient = -7 / 2;\n"   // truncates toward zero
			"const int32 Remainder = -7 % 3;\n"  // takes the dividend's sign
			"const int32 Shifted = 1 << 10;\n"
			"const int32 Masked = (0xF0 | 0x0F) & ~0x3C ^ 0x1;\n"
			"const int32 Negated = -(3 * 4);\n"
			"const bool Less = 3 < 4;\n"
			"const bool Both = (1 < 2) && (2 > 3);\n"
			"const int32 Chosen = Less ? 10 : 20;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("Sum"), int64(12));
		TEST_ASSERT_EQ(program.Int("Difference"), int64(-5));
		TEST_ASSERT_EQ(program.Int("Quotient"), int64(-3));
		TEST_ASSERT_EQ(program.Int("Remainder"), int64(-1));
		TEST_ASSERT_EQ(program.Int("Shifted"), int64(1024));
		TEST_ASSERT_EQ(program.Int("Masked"), int64((0xF0 | 0x0F) & ~0x3C ^ 0x1));
		TEST_ASSERT_EQ(program.Int("Negated"), int64(-12));
		TEST_ASSERT(program.Of("Less").value.booleanValue);
		TEST_ASSERT(!program.Of("Both").value.booleanValue);
		TEST_ASSERT_EQ(program.Int("Chosen"), int64(10));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::FloatsAndBoolsFold()
	{
		Constants program(
			"const double Half = 1.0 / 2.0;\n"
			"const double Mixed = 3 * 0.5;\n"
			"const bool Not = !true;\n"
			"const bool Equal = 2.5 == 2.5;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT(program.Of("Half").value.kind == ConstantValue::Kind::Float64);
		TEST_ASSERT_EQ(program.Of("Half").value.floatValue, 0.5);
		TEST_ASSERT_EQ(program.Of("Mixed").value.floatValue, 1.5);
		TEST_ASSERT(program.Of("Not").value.kind == ConstantValue::Kind::Bool);
		TEST_ASSERT(!program.Of("Not").value.booleanValue);
		TEST_ASSERT(program.Of("Equal").value.booleanValue);
		TEST_PASSED();
	}

	void ConstEvaluatorTest::SizesFold()
	{
		Constants program(
			"struct Pair { public int32 a; public int64 b; }\n"
			"const uint64 PairBytes = sizeof(Pair);\n"
			"const uint64 Elements = sizeof(int32[6]) / sizeof(int32);\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("PairBytes"), int64(16));
		TEST_ASSERT_EQ(program.Int("Elements"), int64(6));
		TEST_PASSED();
	}

	// BUG-205: a cast and `~` did not fold, so an array could not be sized from an enum class member.
	void ConstEvaluatorTest::CastsAndComplementFold()
	{
		Constants program(
			"enum class Size : int32 { One = 1, Two }\n"
			"const int32 FromEnum = (int32)Size::Two;\n"
			"const int64 Static = static_cast<int64>(Size::Two) * 3;\n"
			"const uint8 Wrapped = (uint8)300;\n"
			"const int8 Negative = (int8)200;\n"
			"const int32 Truncated = (int32)2.9;\n"
			"const int32 TowardZero = (int32)-2.9;\n"
			"const float Single = (float)0.1;\n"
			"const bool Truth = (bool)5;\n"
			"const int32 Complement = ~0x0F;\n"
			"const uint16 Masked = (uint16)~0;\n"
			"int32[(int32)Size::Two + ~(~3)] sized;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("FromEnum"), int64(2));
		TEST_ASSERT_EQ(program.Int("Static"), int64(6));
		TEST_ASSERT_EQ(program.Int("Wrapped"), int64(44));
		TEST_ASSERT_EQ(program.Int("Negative"), int64(-56));
		TEST_ASSERT_EQ(program.Int("Truncated"), int64(2));
		TEST_ASSERT_EQ(program.Int("TowardZero"), int64(-2));
		TEST_ASSERT_EQ(program.Of("Single").value.floatValue, static_cast<double>(0.1f));
		TEST_ASSERT(program.Of("Truth").value.booleanValue);
		TEST_ASSERT_EQ(program.Int("Complement"), int64(-16));
		TEST_ASSERT_EQ(program.Int("Masked"), int64(0xFFFF));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::ACastFoldsOnlyWhatHasAConstantValue()
	{
		Constants program(
			"const int32 TooBig = (int32)1e20;\n"
			"const double Real = 2.5;\n"
			"const int32 NotInteger = ~Real;\n"
			"const int32 Shifted = 1.5 << 2;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT(Contains(program.Of("TooBig").message.c_str(), "outside the range of 'int32'"));
		TEST_ASSERT(Contains(program.Of("NotInteger").message.c_str(), "'~' needs an integer operand"));
		TEST_ASSERT(Contains(program.Of("Shifted").message.c_str(), "needs integer operands"));
		TEST_PASSED();
	}

	// BUG-206: these crashed the compiler (INT64_MIN / -1 traps) or were undefined behavior in it.
	void ConstEvaluatorTest::IntegerEdgesHaveTheRunTimesValue()
	{
		Constants program(
			"const int64 Min = -9223372036854775807 - 1;\n"
			"const int64 Max = 9223372036854775807;\n"
			"const int64 Quotient = Min / -1;\n"
			"const int64 Remainder = Min % -1;\n"
			"const int64 Overflow = Max + 1;\n"
			"const int64 Product = Max * 2;\n"
			"const int64 NegatedMin = -Min;\n"
			"const int64 TopBit = 1 << 63;\n"
			"const int64 Wide = 1 << 64;\n"
			"const int64 Backwards = 1 >> -1;\n"
			"const double Infinite = 1.0 / 0.0;\n"
			"int32 main() { return 0; }\n");
		TEST_ASSERT_EQ(program.Int("Quotient"), INT64_MIN);
		TEST_ASSERT_EQ(program.Int("Remainder"), int64(0));
		TEST_ASSERT_EQ(program.Int("Overflow"), INT64_MIN);
		TEST_ASSERT_EQ(program.Int("Product"), int64(-2));
		TEST_ASSERT_EQ(program.Int("NegatedMin"), INT64_MIN);
		TEST_ASSERT_EQ(program.Int("TopBit"), INT64_MIN);
		TEST_ASSERT(Contains(program.Of("Wide").message.c_str(), "shift count 64 is outside 0 to 63"));
		TEST_ASSERT(Contains(program.Of("Backwards").message.c_str(), "shift count -1 is outside 0 to 63"));
		TEST_ASSERT(Contains(program.Of("Infinite").message.c_str(), "division by zero"));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::AnUnresolvedEnumSaysWhy()
	{
		Constants program(
			"enum class Broken : int32 { First, Second = Missing + 1 }\n"
			"int32 main() { return 0; }\n");
		const std::string diagnostics = program.Diagnostics();
		TEST_ASSERT(Contains(diagnostics, "unresolved enum 'Broken': member 'Second':"));
		TEST_ASSERT(Contains(diagnostics, "Missing"));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::ACycleIsReported()
	{
		Constants program(
			"const int32 First = Second + 1;\n"
			"const int32 Second = First + 1;\n"
			"int32 main() { return 0; }\n");
		const QueryResult<ConstantValue> first = program.Of("First");
		TEST_ASSERT(!first.Ok());
		TEST_ASSERT(Contains(first.message.c_str(), "depends on itself"));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::DivisionByZeroDoesNotFold()
	{
		Constants program(
			"const int32 Zero = 0;\n"
			"const int32 Bad = 10 / Zero;\n"
			"int32 main() { return 0; }\n");
		const QueryResult<ConstantValue> bad = program.Of("Bad");
		TEST_ASSERT(!bad.Ok());
		TEST_ASSERT(Contains(bad.message.c_str(), "division by zero"));
		TEST_PASSED();
	}

	void ConstEvaluatorTest::ARunTimeValueDoesNotFold()
	{
		Constants program(
			"int32 g_seed = 4;\n"
			"const int32 Derived = g_seed * 2;\n"
			"int32 main() { return Derived - 8; }\n");
		TEST_ASSERT(!program.Of("g_seed").Ok());
		TEST_ASSERT(!program.Of("Derived").Ok());
		TEST_ASSERT(!program.Of("Missing").Ok());
		TEST_PASSED();
	}
} // namespace Testing
