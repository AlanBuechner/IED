#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// The value lifecycle (docs/language/06, D-03..D-07): what a type needs to be constructed, copied and
	// destroyed, the functions the compiler writes for it, and how lowering uses them for copies,
	// relocation, temporaries and noexcept calls.
	CLASS(GROUP = Testing) LifecycleTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LifecycleTest);

		// --- facts --------------------------------------------------------------------
		TEST_FUNC(Semantic.Lifecycle)
		void PlainTypesAreTrivial();
		TEST_FUNC(Semantic.Lifecycle)
		void NeedsComeFromMembersAndBases();
		TEST_FUNC(Semantic.Lifecycle)
		void EmptyCreatesRunNothing();
		TEST_FUNC(Semantic.Lifecycle)
		void OwnMethodsAreFound();
		TEST_FUNC(Semantic.Lifecycle)
		void CopyabilityNamesTheFix();

		// --- compiler-written functions -------------------------------------------------
		TEST_FUNC(Semantic.Lifecycle)
		void SynthesizedFunctionsAreSharedPerType();

		// --- lowering -------------------------------------------------------------------
		TEST_FUNC(Semantic.Lifecycle)
		void CopyInitializationCallsTheCopyOperator();
		TEST_FUNC(Semantic.Lifecycle)
		void FreshValuesRelocateWithoutCopying();
		TEST_FUNC(Semantic.Lifecycle)
		void DeclarationCleanupOutlivesItsTemporaries();
		TEST_FUNC(Semantic.Lifecycle)
		void ConditionTemporariesEndBeforeTheBranch();
		TEST_FUNC(Semantic.Lifecycle)
		void NoexceptCalleesAreNotInvoked();
	};
} // namespace Testing
