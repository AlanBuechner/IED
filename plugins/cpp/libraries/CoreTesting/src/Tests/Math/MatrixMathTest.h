#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) MatrixMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::MatrixMathTest);

		TEST_FUNC(Math.Matrix.Construction)
		void TestMatrixIdentity();

		TEST_FUNC(Math.Matrix.Arithmetic)
		void TestMatrixMultiplication();

		TEST_FUNC(Math.Matrix.Arithmetic)
		void TestMatrixTranspose();

		TEST_FUNC(Math.Matrix.Analysis)
		void TestMatrixDeterminant();

		TEST_FUNC(Math.Matrix.Analysis)
		void TestMatrixInverse();

		TEST_FUNC(Math.Matrix.Transform)
		void TestMatrixVectorTransform();

		TEST_FUNC(Math.Matrix.Transform)
		void TestMatrixQuatCast();
	};
}
