#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R8: the macro pass lays its work over the file's own text (Macro/MacroLayout.h), and the lexer reads the file through
	// it: every token keeps the file's position, and one from a macro knows its expansion.
	CLASS(GROUP = Testing) MacroLayoutTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MacroLayoutTest);

		TEST_FUNC(Macro.Layout)
		void UsesAreExpansionsOverTheFilesText();

		TEST_FUNC(Macro.Layout)
		void DirectivesAndInactiveGroupsAreLeftOut();

		TEST_FUNC(Macro.Layout)
		void ArgumentTokensKeepTheirPlace();

		TEST_FUNC(Macro.Layout)
		void RenderKeepsEveryLine();

		TEST_FUNC(Macro.Layout)
		void RenderedOffsetsFollowTheExpansions();

		TEST_FUNC(Macro.Layout)
		void UnchangedUseIsNoExpansion();

		TEST_FUNC(Macro.Layout)
		void LexerLocatesTokensOfAnExpansion();

		TEST_FUNC(Macro.Layout)
		void LexerKeepsTheColumnsAfterAUse();

		TEST_FUNC(Macro.Layout)
		void LexerJoinsOperatorsOnlyWithinAnExpansion();

		TEST_FUNC(Macro.Layout)
		void LexerPassesOverLeftOutLines();

		TEST_FUNC(Macro.Layout)
		void ExpansionKnowsWhereItsMacroIsDefined();
	};
}
