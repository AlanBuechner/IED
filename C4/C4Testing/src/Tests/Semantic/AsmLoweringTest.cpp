#include "AsmLoweringTest.h"
#include "Backend/HIR/AsmTemplate.h"
#include "Backend/HIR/AsmRegisters.h"

namespace Testing
{
	namespace
	{
		// Operands a, b, c are numbered 0, 1, 2; `{0}`.. are passed through as positions.
		int IndexOf(const std::string& name)
		{
			if (name == "a") return 0;
			if (name == "b") return 1;
			if (name == "c") return 2;
			if (!name.empty() && name[0] >= '0' && name[0] <= '9') return std::stoi(name) < 3 ? std::stoi(name) : -1;
			return -1;
		}

		std::string Translate(std::vector<std::string> lines)
		{
			auto result = Scripting::AsmTemplate::Translate(lines, IndexOf);
			return result.Ok() ? result.value : "ERROR: " + std::string(result.message.c_str());
		}

		bool Fails(std::vector<std::string> lines, const char* fragment)
		{
			auto result = Scripting::AsmTemplate::Translate(lines, IndexOf);
			return !result.Ok() && std::string(result.message.c_str()).find(fragment) != std::string::npos;
		}
	}

	void AsmLoweringTest::TemplateNamesAndPositions()
	{
		TEST_ASSERT(Translate({ "mov {a}, {b}" }) == "mov ${0}, ${1}");
		TEST_ASSERT(Translate({ "add {c}, {0}", "ret" }) == "add ${2}, ${0}\n\tret");
		TEST_ASSERT(Translate({}) == "");
		TEST_ASSERT(Translate({ "nop" }) == "nop");
		TEST_PASSED();
	}

	void AsmLoweringTest::TemplateModifiersAndEscapes()
	{
		TEST_ASSERT(Translate({ "mov {a:k}, {b:q}" }) == "mov ${0:k}, ${1:q}");
		TEST_ASSERT(Translate({ "{{literal}} {a}" }) == "{literal} ${0}");
		TEST_ASSERT(Translate({ "movl $1, {a}" }) == "movl $$1, ${0}"); // a `$` is LLVM's escape character
		TEST_PASSED();
	}

	void AsmLoweringTest::TemplateErrors()
	{
		TEST_ASSERT(Fails({ "mov {d}, 1" }, "'{d}', which is not an operand"));
		TEST_ASSERT(Fails({ "mov {7}, 1" }, "'{7}', which is not an operand"));
		TEST_ASSERT(Fails({ "mov {a:z}, 1" }, "'z' in '{a:z}' is not an x86 operand modifier"));
		TEST_ASSERT(Fails({ "mov {a, 1" }, "is not closed"));
		TEST_ASSERT(Fails({ "mov a}, 1" }, "closes nothing"));
		TEST_ASSERT(Fails({ "ok {a}", "bad {x}" }, "'{x}'")); // the second line is checked too
		TEST_PASSED();
	}

	void AsmLoweringTest::RegisterKinds()
	{
		using Scripting::AsmRegisters::Kind;
		using Scripting::AsmRegisters::KindOf;
		for (const char* name : { "rax", "eax", "ax", "al", "ah", "rsp", "sil", "r8", "r15d", "r9w", "r10b" })
			TEST_ASSERT(KindOf(name) == Kind::General);
		for (const char* name : { "xmm0", "xmm31", "ymm7", "zmm16" })
			TEST_ASSERT(KindOf(name) == Kind::Vector);
		for (const char* name : { "", "eaxx", "r7", "r16", "r8q", "xmm32", "xmm01", "mm0", "zmm", "k1" })
			TEST_ASSERT(!KindOf(name).has_value());
		TEST_ASSERT(Scripting::AsmRegisters::CallerSaved().size() == 13);
		TEST_PASSED();
	}

	void AsmLoweringTest::RegisterFamilies()
	{
		using Scripting::AsmRegisters::Family;
		TEST_ASSERT(Family("eax") == "rax" && Family("al") == "rax" && Family("ah") == "rax" && Family("ax") == "rax");
		TEST_ASSERT(Family("sil") == "rsi" && Family("esp") == "rsp");
		TEST_ASSERT(Family("r9d") == "r9" && Family("r12b") == "r12" && Family("r15") == "r15");
		TEST_ASSERT(Family("ymm3") == "xmm3" && Family("zmm20") == "xmm20" && Family("xmm1") == "xmm1");
		TEST_PASSED();
	}
} // namespace Testing
