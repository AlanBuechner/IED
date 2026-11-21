#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R7: a declaration, member or statement that does not parse is one error, skipped to the next one (an ErrorNode in the
	// tree), so the rest of the file parses as if it were not there.
	CLASS(GROUP = Testing) ParserRecoveryTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ParserRecoveryTest);

		TEST_FUNC(Parser.Recovery)
		void MemberThatDoesNotParseIsOneError();

		TEST_FUNC(Parser.Recovery)
		void StatementThatDoesNotParseIsOneError();

		TEST_FUNC(Parser.Recovery)
		void MissingParenBeforeBlockIsOneError();

		TEST_FUNC(Parser.Recovery)
		void MissingParenAfterACallIsOneError();

		TEST_FUNC(Parser.Recovery)
		void BrokenLambdaInACallIsOneError();

		TEST_FUNC(Parser.Recovery)
		void UnclosedTypeBodyEndsByIndentation();

		TEST_FUNC(Parser.Recovery)
		void UnclosedBlockInMethodEndsAtOuterBrace();

		TEST_FUNC(Parser.Recovery)
		void ExtraClosingBraceIsLeftOut();

		TEST_FUNC(Parser.Recovery)
		void StrayBraceAtRootIsOneError();

		TEST_FUNC(Parser.Recovery)
		void GarbageSkipsToNextDeclaration();

		TEST_FUNC(Parser.Recovery)
		void FunctionWithBrokenParametersKeepsTheRest();

		TEST_FUNC(Parser.Recovery)
		void NamespaceOpenAtEndOfFileIsAnError();

		TEST_FUNC(Parser.Recovery)
		void WellFormedFileHasNoErrorNode();
	};
}
