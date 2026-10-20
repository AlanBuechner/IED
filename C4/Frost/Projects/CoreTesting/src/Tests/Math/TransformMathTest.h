#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) TransformMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::TransformMathTest);

		TEST_FUNC(Math.Transform.MatrixOps)
		void TestTranslate();

		TEST_FUNC(Math.Transform.MatrixOps)
		void TestScale();

		TEST_FUNC(Math.Transform.MatrixOps)
		void TestRotate();

		TEST_FUNC(Math.Transform.Projection)
		void TestOrtho();

		TEST_FUNC(Math.Transform.Projection)
		void TestPerspective();

		TEST_FUNC(Math.Transform.Projection)
		void TestLookAt();
	};
}
