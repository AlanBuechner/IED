#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"

namespace Testing
{
	// D-91 (R6): the `[compile_time]` mark, the check where a marked function is defined (Backend/MIR/CompileTimeCheck),
	// and the evaluator a constant calls (Backend/MIR/CompileTimeEvaluator).
	CLASS(GROUP = Testing) CompileTimeTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::CompileTimeTest);

		TEST_FUNC(Mir.CompileTime)
		void TheMarkIsOnAnyDeclaration();
		TEST_FUNC(Mir.CompileTime)
		void AMarkedFunctionThatCanRunCompiles();
		TEST_FUNC(Mir.CompileTime)
		void AMarkedFunctionMayDoOnlyWhatCompileTimeRuns();
		TEST_FUNC(Mir.CompileTime)
		void TheMarkTakesNoArguments();
		TEST_FUNC(Mir.CompileTime)
		void TheEvaluatorRunsAMarkedFunction();
		TEST_FUNC(Mir.CompileTime)
		void TheEvaluatorRefusesWhatItCannotRun();
		TEST_FUNC(Mir.CompileTime)
		void DataComesBackAsTheTargetLaysItOut();
		TEST_FUNC(Mir.CompileTime)
		void EveryConstantPositionCallsThroughTheModel();
		TEST_FUNC(Mir.CompileTime)
		void AConstantCallsOnlyAMarkedFunction();
		TEST_FUNC(Mir.CompileTime)
		void AGlobalTableStartsAsData();
	};
} // namespace Testing
