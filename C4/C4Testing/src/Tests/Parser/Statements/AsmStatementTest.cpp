#include "AsmStatementTest.h"
#include "SourceManager/Source.h"
#include "Compiler/ErrorTracker.h"

namespace Testing
{
	namespace
	{
		struct Parsed
		{
			Ref<Scripting::AsmStatementNode> node;
			uint32 errors = 0;
		};

		Parsed Parse(const char* text)
		{
			auto stream = Scripting::Source::FromMemory(text);
			Scripting::Lexer lexer(*stream, 0);
			Scripting::ErrorTracker errorTracker;
			Scripting::ParserState state(lexer, errorTracker);
			Parsed parsed;
			parsed.node = Scripting::AsmStatementNode::TryParse(state);
			parsed.errors = errorTracker.GetErrors().Count();
			return parsed;
		}
	}

	void AsmStatementTest::LinesAndOptions()
	{
		Parsed p = Parse("asm(nomem, pure) { \"mov eax, 1\"; \"add eax, \" \"2\"; }");
		TEST_ASSERT(p.node != nullptr && p.errors == 0);
		TEST_ASSERT(p.node->GetLines().Count() == 2);
		TEST_ASSERT(p.node->GetLines()[0] == "mov eax, 1");
		TEST_ASSERT(p.node->GetLines()[1] == "add eax, 2"); // adjacent strings join into one line
		TEST_ASSERT(p.node->GetOptions().Count() == 2);
		TEST_ASSERT(p.node->GetOptions()[1].GetText() == "pure");
		TEST_PASSED();
	}

	void AsmStatementTest::OperandKinds()
	{
		Parsed p = Parse("asm { \"x\"; in(reg) a; out(\"eax\") low; inout(xmm) v; in(reg) n = a + 1; out(reg) o = cells[2]; const k = 3; in(reg) a * 2; }");
		TEST_ASSERT(p.node != nullptr && p.errors == 0);
		const auto& ops = p.node->GetOperands();
		TEST_ASSERT(ops.Count() == 7);
		TEST_ASSERT(ops[0].kind == Scripting::AsmOperand::Kind::In && ops[0].name == "a" && ops[0].registerClass == "reg");
		TEST_ASSERT(ops[1].kind == Scripting::AsmOperand::Kind::Out && ops[1].explicitRegister && ops[1].registerClass == "eax");
		TEST_ASSERT(ops[2].kind == Scripting::AsmOperand::Kind::InOut && ops[2].name == "v" && ops[2].registerClass == "xmm");
		TEST_ASSERT(ops[3].name == "n" && ops[3].value != nullptr);
		TEST_ASSERT(ops[4].name == "o");
		TEST_ASSERT(ops[5].kind == Scripting::AsmOperand::Kind::Const && ops[5].name == "k");
		TEST_ASSERT(ops[6].name.empty()); // an expression with no name is positional only
		TEST_ASSERT(p.node->Children().size() == 7);
		TEST_PASSED();
	}

	void AsmStatementTest::Clobbers()
	{
		Parsed p = Parse("asm { \"x\"; clobber(\"rcx\", \"memory\"); clobber_abi(\"C\"); }");
		TEST_ASSERT(p.node != nullptr && p.errors == 0);
		TEST_ASSERT(p.node->GetClobbers().Count() == 2);
		TEST_ASSERT(p.node->GetClobbers()[1] == "memory");
		TEST_ASSERT(p.node->ClobbersCallerSaved());
		Parsed bad = Parse("asm { clobber_abi(\"sysv\"); }");
		TEST_ASSERT(bad.errors == 1);
		TEST_PASSED();
	}

	void AsmStatementTest::LastItemMayOmitSemicolon()
	{
		Parsed p = Parse("asm { \"nop\"; in(reg) a }");
		TEST_ASSERT(p.node != nullptr && p.errors == 0);
		TEST_ASSERT(p.node->GetOperands().Count() == 1);
		TEST_PASSED();
	}

	void AsmStatementTest::MalformedItemsRecover()
	{
		// An unknown word, an operand with no register and a missing ';' each report one error; the block still ends.
		Parsed p = Parse("asm { mov eax, 1; in a; \"nop\" \"x\" in(reg) b; \"y\"; }");
		TEST_ASSERT(p.node != nullptr);
		TEST_ASSERT(p.errors >= 3);
		TEST_ASSERT(p.node->GetLines().Count() >= 1);
		Parsed noBlock = Parse("asm \"nop\";");
		TEST_ASSERT(noBlock.errors == 1);
		TEST_PASSED();
	}

	void AsmStatementTest::NotAnAsmStatement()
	{
		TEST_ASSERT(Parse("x = 1;").node == nullptr);
		TEST_ASSERT(Parse("asmx;").node == nullptr);
		TEST_PASSED();
	}
} // namespace Testing
