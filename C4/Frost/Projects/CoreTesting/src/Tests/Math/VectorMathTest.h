#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) VectorMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::VectorMathTest);

		TEST_FUNC(Math.Vector.Construction)
		void TestVectorConstructors();

		TEST_FUNC(Math.Vector.Arithmetic)
		void TestVectorArithmetic();

		TEST_FUNC(Math.Vector.Arithmetic)
		void TestDotProduct();

		TEST_FUNC(Math.Vector.Arithmetic)
		void TestCrossProduct();

		TEST_FUNC(Math.Vector.Geometry)
		void TestLengthAndDistance();

		TEST_FUNC(Math.Vector.Geometry)
		void TestNormalize();

		TEST_FUNC(Math.Vector.Operations)
		void TestVectorLerpClampMinMax();

		TEST_FUNC(Math.Vector.Operations)
		void TestVectorAbsFloorCeil();
	};
}
