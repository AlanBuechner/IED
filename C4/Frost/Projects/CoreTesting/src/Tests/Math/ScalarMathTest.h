#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) ScalarMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::ScalarMathTest);

		TEST_FUNC(Math.Scalar.BasicFunctions)
		void TestScalarTrigonometry();

		TEST_FUNC(Math.Scalar.BasicFunctions)
		void TestScalarAngleConversion();

		TEST_FUNC(Math.Scalar.BasicFunctions)
		void TestScalarExponentialLogarithmic();

		TEST_FUNC(Math.Scalar.Operations)
		void TestScalarRounding();

		TEST_FUNC(Math.Scalar.Operations)
		void TestScalarClampStepSmoothstep();

		TEST_FUNC(Math.Scalar.Operations)
		void TestScalarInterpolation();
	};
}
