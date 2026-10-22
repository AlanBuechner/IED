#pragma once

#include "UnitTesting/TestBase.h"
#include "Math/Math.h"
#include "GLMInclude.h"
#include "MathTestHelpers.h"

namespace Testing
{
	CLASS(GROUP = Testing) QuaternionMathTest : public Test
	{
	public:
		REFLECTED_BODY(Testing::QuaternionMathTest);

		TEST_FUNC(Math.Quaternion.Construction)
		void TestQuaternionIdentity();

		TEST_FUNC(Math.Quaternion.Construction)
		void TestAngleAxis();

		TEST_FUNC(Math.Quaternion.Construction)
		void TestEulerConstructor();

		TEST_FUNC(Math.Quaternion.Arithmetic)
		void TestQuaternionConjugateInverse();

		TEST_FUNC(Math.Quaternion.Arithmetic)
		void TestQuaternionNormalize();

		TEST_FUNC(Math.Quaternion.Arithmetic)
		void TestQuaternionMultiply();

		TEST_FUNC(Math.Quaternion.Transform)
		void TestRotateVector();

		TEST_FUNC(Math.Quaternion.Interpolation)
		void TestQuaternionLerpSlerp();

		TEST_FUNC(Math.Quaternion.Transform)
		void TestEulerAngles();

		TEST_FUNC(Math.Quaternion.Transform)
		void TestQuaternionMatrixCast();

		TEST_FUNC(Math.Quaternion.Transform)
		void TestQuaternionVectorMultiply();
	};
}
