#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// Whole-program nounwind inference (Backend/CodeGen/UnwindInference.h) over modules of their own, as a split
	// program's partitions are, on several threads.
	CLASS(GROUP = Testing) UnwindInferenceTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::UnwindInferenceTest);

		TEST_FUNC(Compiler.UnwindInference)
		void InvokesOfFunctionsThatCannotUnwindBecomeCalls();
		TEST_FUNC(Compiler.UnwindInference)
		void ExceptionPathsNothingReachesAreDropped();
		TEST_FUNC(Compiler.UnwindInference)
		void RecursionWithoutThrowsCannotUnwind();
		TEST_FUNC(Compiler.UnwindInference)
		void CallsThroughPointersAndUnknownDeclarationsUnwind();
		TEST_FUNC(Compiler.UnwindInference)
		void FunctionsThatCannotUnwindKeepTheirUnwindTables();
		TEST_FUNC(Compiler.UnwindInference)
		void LocalFunctionsAreTheirModulesOwn();
	};
} // namespace Testing
