#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// C4Api versions 2 and 3 (docs/tools/06-c4-api.md section 2b): a program opened, analyzed and queried through the C interface,
	// against a small program of three files.
	CLASS(GROUP = Testing) ProgramApiTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ProgramApiTest);

		TEST_FUNC(Api.Program)
		void ACleanProgramOpensWithoutDiagnostics();

		TEST_FUNC(Api.Program)
		void DiagnosticsNameTheirFileAndPlace();

		TEST_FUNC(Api.Program)
		void AReferenceResolvesToItsDeclarationInAnotherFile();

		TEST_FUNC(Api.Program)
		void ADeclarationsOwnNameResolvesToItself();

		TEST_FUNC(Api.Program)
		void NothingResolvesWhereNothingIsNamed();

		TEST_FUNC(Api.Program)
		void TheTypeOfAnExpressionAndOfADeclarationIsTold();

		TEST_FUNC(Api.Program)
		void ReferencesListEveryUseAcrossFiles();

		TEST_FUNC(Api.Program)
		void AnObjectsMembersAreListedByNameAndByPlace();

		TEST_FUNC(Api.Program)
		void ANullProgramOrArgumentAnswersNothing();

		TEST_FUNC(Api.Program)
		void MembersWithBasesNameTheBaseTheyComeFrom();

		TEST_FUNC(Api.Program)
		void ADeclarationsOnlyProgramSkipsTheBodies();
	};
}
