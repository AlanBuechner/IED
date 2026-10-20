#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) DecompositionMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::DecompositionMathTest);

		TEST_FUNC(Math.Decomposition.DecomposeTransform)
		void TestDecomposeTransform();
	};
}
