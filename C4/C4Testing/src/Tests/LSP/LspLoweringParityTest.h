#pragma once
#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// R2: the editor's body diagnostics are the build's. The language server and C4.exe lower the same functions in the
	// same order (ProgramLowering), so a member of a class-template instance nothing uses is checked by neither (D-16),
	// and one the program uses - by a call, a vtable, a global's storage or start - is reported by both, alike.
	CLASS(GROUP = Testing) LspLoweringParityTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LspLoweringParityTest);

		TEST_FUNC(Lsp.LoweringParity)
		void AnUnusedMemberOfAnInstanceIsCheckedByNeither();

		TEST_FUNC(Lsp.LoweringParity)
		void ACalledMemberIsReportedAlikeWithItsUse();

		TEST_FUNC(Lsp.LoweringParity)
		void AVirtualMemberOfAMadeInstanceIsReportedByBoth();

		TEST_FUNC(Lsp.LoweringParity)
		void AVirtualMemberOfAnInstanceOnlyPointedAtIsReportedByBoth();

		TEST_FUNC(Lsp.LoweringParity)
		void AGlobalOfAnInstanceNeedsItsVirtualMembers();

		TEST_FUNC(Lsp.LoweringParity)
		void AGlobalStartReachesTheMembersItUses();

		TEST_FUNC(Lsp.LoweringParity)
		void AScriptHostIsLoweredAlikeInBoth();

		TEST_FUNC(Lsp.LoweringParity)
		void TheProgramOptionsSayWhetherItIsAScriptHost();

		TEST_FUNC(Lsp.LoweringParity)
		void ACleanProgramIsCleanInBoth();

		// D-91: a [compile_time] body is checked, and a constant that calls one folds, alike in both.
		TEST_FUNC(Lsp.LoweringParity)
		void ACompileTimeBodyIsCheckedAlikeInBoth();
		TEST_FUNC(Lsp.LoweringParity)
		void AConstantCallIsFoldedAlikeInBoth();

		// BUG-487: a call of a member the type does not have, in a file-static function, is the build's error in both.
		TEST_FUNC(Lsp.LoweringParity)
		void AMissingMemberIsReportedByBoth();
	};
} // namespace Testing
