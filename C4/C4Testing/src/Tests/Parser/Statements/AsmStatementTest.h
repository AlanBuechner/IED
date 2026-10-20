#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"

namespace Testing
{
	// Parser/Nodes/Statements/AsmStatementNode (docs/language/16): lines, operands, options, clobbers and errors.
	CLASS(GROUP = Testing) AsmStatementTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::AsmStatementTest);

		TEST_FUNC(Parser.Statements.Asm)
		void LinesAndOptions();
		TEST_FUNC(Parser.Statements.Asm)
		void OperandKinds();
		TEST_FUNC(Parser.Statements.Asm)
		void Clobbers();
		TEST_FUNC(Parser.Statements.Asm)
		void LastItemMayOmitSemicolon();
		TEST_FUNC(Parser.Statements.Asm)
		void MalformedItemsRecover();
		TEST_FUNC(Parser.Statements.Asm)
		void NotAnAsmStatement();
	};
} // namespace Testing
