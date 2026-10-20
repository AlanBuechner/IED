#pragma once

#include "UnitTesting/TestBase.h"
#include "UnitTesting/TestAssertions.h"
#include "Compiler/CompilerInstance.h"
#include "Model/ProgramModel.h"

namespace Testing
{
	// Lambdas and closures (docs/language/07, D-22, D-23): capture lists, the body's outer uses, closure types,
	// func<R(A)> and the conversions of callables.
	CLASS(GROUP = Testing) LambdaTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::LambdaTest);

		TEST_FUNC(Semantic.Lambdas)
		void CaptureListsParse();
		TEST_FUNC(Semantic.Lambdas)
		void OuterUsesSkipParametersLocalsAndMembers();
		TEST_FUNC(Semantic.Lambdas)
		void NestedLambdaUsesCountInTheEnclosingScope();
		TEST_FUNC(Semantic.Lambdas)
		void ClosureTypesHoldTheirCaptures();
		TEST_FUNC(Semantic.Lambdas)
		void FuncTypesAreSixteenBytesAndInterned();
		TEST_FUNC(Semantic.Lambdas)
		void CallablesConvertToFuncAndFuncptr();
		TEST_FUNC(Semantic.Lambdas)
		void FunctionPointersBindOnlyVoidPointers();
		TEST_FUNC(Semantic.Lambdas)
		void NestedBodiesDoNotSeeEnclosingLocals();
	};
} // namespace Testing
