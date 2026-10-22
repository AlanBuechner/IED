#include "MatrixMathTest.h"

namespace Testing
{
	using namespace Math;

	void MatrixMathTest::TestMatrixIdentity()
	{
		Math::Mat2 m2 = Math::Mat2::Identity();
		glm::mat2 glmM2(1.0f);
		if (!MatricesEpsilonEqual<2, 2>(ValuePtr(m2), glm::value_ptr(glmM2)))
			TEST_FAILED("Mat2 identity mismatch");

		Math::Mat3 m3 = Math::Mat3::Identity();
		glm::mat3 glmM3(1.0f);
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(m3), glm::value_ptr(glmM3)))
			TEST_FAILED("Mat3 identity mismatch");

		Math::Mat4 m4 = Math::Mat4::Identity();
		glm::mat4 glmM4(1.0f);
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(m4), glm::value_ptr(glmM4)))
			TEST_FAILED("Mat4 identity mismatch");

		// Zero matrices
		Math::Mat4 mz = Math::Mat4::Zero();
		glm::mat4 glmMz(0.0f);
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(mz), glm::value_ptr(glmMz)))
			TEST_FAILED("Mat4 zero mismatch");

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixMultiplication()
	{
		Math::Mat4 a(
			Vector4(1.0f, 2.0f, 3.0f, 4.0f),
			Vector4(5.0f, 6.0f, 7.0f, 8.0f),
			Vector4(9.0f, 10.0f, 11.0f, 12.0f),
			Vector4(13.0f, 14.0f, 15.0f, 16.0f));

		Math::Mat4 b(
			Vector4(16.0f, 15.0f, 14.0f, 13.0f),
			Vector4(12.0f, 11.0f, 10.0f, 9.0f),
			Vector4(8.0f, 7.0f, 6.0f, 5.0f),
			Vector4(4.0f, 3.0f, 2.0f, 1.0f));

		Math::Mat4 prod = a * b;

		glm::mat4 glmA(
			1.0f, 2.0f, 3.0f, 4.0f,
			5.0f, 6.0f, 7.0f, 8.0f,
			9.0f, 10.0f, 11.0f, 12.0f,
			13.0f, 14.0f, 15.0f, 16.0f);

		glm::mat4 glmB(
			16.0f, 15.0f, 14.0f, 13.0f,
			12.0f, 11.0f, 10.0f, 9.0f,
			8.0f, 7.0f, 6.0f, 5.0f,
			4.0f, 3.0f, 2.0f, 1.0f);

		glm::mat4 glmProd = glmA * glmB;

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(prod), glm::value_ptr(glmProd)))
			TEST_FAILED("Mat4 multiplication mismatch");

		// Mat3 multiplication
		Math::Mat3 m3a(2.0f);
		Math::Mat3 m3b = Math::Mat3::Identity();
		Math::Mat3 m3prod = m3a * m3b;
		glm::mat3 glmM3a(2.0f);
		glm::mat3 glmM3b(1.0f);
		glm::mat3 glmM3prod = glmM3a * glmM3b;
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(m3prod), glm::value_ptr(glmM3prod)))
			TEST_FAILED("Mat3 multiplication mismatch");

		// Scalar multiplication
		Math::Mat4 scaled = a * 2.0f;
		glm::mat4 glmScaled = glmA * 2.0f;
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(scaled), glm::value_ptr(glmScaled)))
			TEST_FAILED("Mat4 scalar multiplication mismatch");

		// Matrix addition/subtraction
		Math::Mat4 sum = a + b;
		glm::mat4 glmSum = glmA + glmB;
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(sum), glm::value_ptr(glmSum)))
			TEST_FAILED("Mat4 addition mismatch");

		Math::Mat4 diff = a - b;
		glm::mat4 glmDiff = glmA - glmB;
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(diff), glm::value_ptr(glmDiff)))
			TEST_FAILED("Mat4 subtraction mismatch");

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixTranspose()
	{
		Math::Mat4 m(
			Vector4(1.0f, 2.0f, 3.0f, 4.0f),
			Vector4(5.0f, 6.0f, 7.0f, 8.0f),
			Vector4(9.0f, 10.0f, 11.0f, 12.0f),
			Vector4(13.0f, 14.0f, 15.0f, 16.0f));

		Math::Mat4 t = m.Transpose();
		glm::mat4 glmM(
			1.0f, 2.0f, 3.0f, 4.0f,
			5.0f, 6.0f, 7.0f, 8.0f,
			9.0f, 10.0f, 11.0f, 12.0f,
			13.0f, 14.0f, 15.0f, 16.0f);

		glm::mat4 glmT = glm::transpose(glmM);
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(t), glm::value_ptr(glmT)))
			TEST_FAILED("Mat4 transpose mismatch");

		// Mat3 transpose
		Math::Mat3 m3(
			Vector3(1.0f, 2.0f, 3.0f),
			Vector3(4.0f, 5.0f, 6.0f),
			Vector3(7.0f, 8.0f, 9.0f));

		Math::Mat3 t3 = m3.Transpose();
		glm::mat3 glmM3(
			1.0f, 2.0f, 3.0f,
			4.0f, 5.0f, 6.0f,
			7.0f, 8.0f, 9.0f);

		glm::mat3 glmT3 = glm::transpose(glmM3);
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(t3), glm::value_ptr(glmT3)))
			TEST_FAILED("Mat3 transpose mismatch");

		// Mat2 transpose
		Math::Mat2 m2(
			Vector2(1.0f, 2.0f),
			Vector2(3.0f, 4.0f));

		Math::Mat2 t2 = m2.Transpose();
		glm::mat2 glmM2(
			1.0f, 2.0f,
			3.0f, 4.0f);

		glm::mat2 glmT2 = glm::transpose(glmM2);
		if (!MatricesEpsilonEqual<2, 2>(ValuePtr(t2), glm::value_ptr(glmT2)))
			TEST_FAILED("Mat2 transpose mismatch");

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixDeterminant()
	{
		// Mat4 determinant
		Math::Mat4 m(
			Vector4(1.0f, 2.0f, 3.0f, 4.0f),
			Vector4(5.0f, 6.0f, 7.0f, 8.0f),
			Vector4(9.0f, 10.0f, 11.0f, 12.0f),
			Vector4(13.0f, 14.0f, 15.0f, 16.0f));

		float det = m.Determinant();
		glm::mat4 glmM(
			1.0f, 2.0f, 3.0f, 4.0f,
			5.0f, 6.0f, 7.0f, 8.0f,
			9.0f, 10.0f, 11.0f, 12.0f,
			13.0f, 14.0f, 15.0f, 16.0f);

		float glmDet = glm::determinant(glmM);
		if (!Math::EpsilonEqual(det, glmDet, 1e-2f))
			TEST_FAILED("Mat4 determinant mismatch: ours={0}, glm={1}", det, glmDet);

		// Identity matrix should have determinant 1
		Math::Mat4 id = Math::Mat4::Identity();
		if (!Math::EpsilonEqual(id.Determinant(), 1.0f))
			TEST_FAILED("Identity determinant is {0}, expected 1.0", id.Determinant());

		// Mat3 determinant
		Math::Mat3 m3(
			Vector3(6.0f, 1.0f, 1.0f),
			Vector3(-4.0f, 2.0f, 5.0f),
			Vector3(2.0f, 8.0f, 7.0f));

		float det3 = m3.Determinant();
		glm::mat3 glmM3(
			6.0f, 1.0f, 1.0f,
			-4.0f, 2.0f, 5.0f,
			2.0f, 8.0f, 7.0f);

		float glmDet3 = glm::determinant(glmM3);
		if (!Math::EpsilonEqual(det3, glmDet3))
			TEST_FAILED("Mat3 determinant mismatch: ours={0}, glm={1}", det3, glmDet3);

		// Mat2 determinant
		Math::Mat2 m2(
			Vector2(1.0f, 2.0f),
			Vector2(3.0f, 4.0f));

		float det2 = m2.Determinant();
		glm::mat2 glmM2(
			1.0f, 2.0f,
			3.0f, 4.0f);

		float glmDet2 = glm::determinant(glmM2);
		if (!Math::EpsilonEqual(det2, glmDet2))
			TEST_FAILED("Mat2 determinant mismatch: ours={0}, glm={1}", det2, glmDet2);

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixInverse()
	{
		// Mat4 inverse - use a non-singular matrix
		Math::Mat4 m(
			Vector4(1.0f, 2.0f, 3.0f, 1.0f),
			Vector4(2.0f, 1.0f, 4.0f, 1.0f),
			Vector4(3.0f, 4.0f, 1.0f, 1.0f),
			Vector4(1.0f, 1.0f, 1.0f, 1.0f));

		Math::Mat4 inv = Inverse(m);
		glm::mat4 glmM(
			1.0f, 2.0f, 3.0f, 1.0f,
			2.0f, 1.0f, 4.0f, 1.0f,
			3.0f, 4.0f, 1.0f, 1.0f,
			1.0f, 1.0f, 1.0f, 1.0f);

		glm::mat4 glmInv = glm::inverse(glmM);
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(inv), glm::value_ptr(glmInv)))
			TEST_FAILED("Mat4 inverse mismatch");

		// Verify M * inv(M) ~= I
		Math::Mat4 check = m * inv;
		Math::Mat4 id = Math::Mat4::Identity();
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(check), ValuePtr(id), 1e-3f))
			TEST_FAILED("M * inv(M) not identity");

		// Mat3 inverse
		Math::Mat3 m3(
			Vector3(6.0f, 1.0f, 1.0f),
			Vector3(-4.0f, 2.0f, 5.0f),
			Vector3(2.0f, 8.0f, 7.0f));

		Math::Mat3 inv3 = Inverse(m3);
		glm::mat3 glmM3(
			6.0f, 1.0f, 1.0f,
			-4.0f, 2.0f, 5.0f,
			2.0f, 8.0f, 7.0f);

		glm::mat3 glmInv3 = glm::inverse(glmM3);
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(inv3), glm::value_ptr(glmInv3)))
			TEST_FAILED("Mat3 inverse mismatch");

		// Mat2 inverse
		Math::Mat2 m2(
			Vector2(1.0f, 2.0f),
			Vector2(3.0f, 4.0f));

		Math::Mat2 inv2 = Inverse(m2);
		glm::mat2 glmM2(
			1.0f, 2.0f,
			3.0f, 4.0f);

		glm::mat2 glmInv2 = glm::inverse(glmM2);
		if (!MatricesEpsilonEqual<2, 2>(ValuePtr(inv2), glm::value_ptr(glmInv2)))
			TEST_FAILED("Mat2 inverse mismatch");

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixVectorTransform()
	{
		// Mat4 * Vector3 (position transform with translation)
		Math::Mat4 m = Translate(Vector3(10.0f, 20.0f, 30.0f));
		Math::Vector3 v(1.0f, 2.0f, 3.0f);
		Math::Vector3 result = m * v;

		glm::mat4 glmM = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f));
		glm::vec3 glmResult = static_cast<glm::vec3>(glmM * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));

		if (!VectorsEpsilonEqual<3>(ValuePtr(result), glm::value_ptr(glmResult)))
			TEST_FAILED("Mat4*Vector3 transform mismatch");

		// Mat4 * Vector4
		Math::Vector4 v4(1.0f, 2.0f, 3.0f, 1.0f);
		Math::Vector4 result4 = m * v4;
		glm::vec4 glmResult4 = glmM * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
		if (!VectorsEpsilonEqual<4>(ValuePtr(result4), glm::value_ptr(glmResult4)))
			TEST_FAILED("Mat4*Vector4 transform mismatch");

		// Mat3 * Vector3
		Math::Mat3 scale3(
			Vector3(2.0f, 0.0f, 0.0f),
			Vector3(0.0f, 3.0f, 0.0f),
			Vector3(0.0f, 0.0f, 4.0f));

		Math::Vector3 v3(1.0f, 2.0f, 3.0f);
		Math::Vector3 result3 = scale3 * v3;

		glm::mat3 glmScale3(
			2.0f, 0.0f, 0.0f,
			0.0f, 3.0f, 0.0f,
			0.0f, 0.0f, 4.0f);

		glm::vec3 glmResult3 = glmScale3 * glm::vec3(1.0f, 2.0f, 3.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(result3), glm::value_ptr(glmResult3)))
			TEST_FAILED("Mat3*Vector3 transform mismatch");

		TEST_PASSED();
	}

	void MatrixMathTest::TestMatrixQuatCast()
	{
		// --- Identity matrix -> quaternion must be identity ---
		Math::Mat3 idM3 = Math::Mat3::Identity();
		Math::Quaternion qFromIdM3 = QuatCast(idM3);
		if (!QuaternionsEpsilonEqual(qFromIdM3, Math::Quaternion::Identity()))
			TEST_FAILED("QuatCast(Mat3 identity) is not identity quaternion");

		Math::Mat4 idM4 = Math::Mat4::Identity();
		Math::Quaternion qFromIdM4 = QuatCast(idM4);
		if (!QuaternionsEpsilonEqual(qFromIdM4, Math::Quaternion::Identity()))
			TEST_FAILED("QuatCast(Mat4 identity) is not identity quaternion");

		// --- 90 deg Y rotation matrix -> quaternion ---
		float angle = Math::PI / 2.0f;
		Vector3 axis(0.0f, 1.0f, 0.0f);
		Math::Quaternion qY = AngleAxis(angle, axis);

		Math::Mat3 m3y = Mat3Cast(qY);
		Math::Quaternion qBackM3y = QuatCast(m3y);
		if (!QuaternionsEpsilonEqual(qBackM3y, qY))
			TEST_FAILED("QuatCast(Mat3Cast(90 deg Y)) round-trip mismatch");

		Math::Mat4 m4y = Mat4Cast(qY);
		Math::Quaternion qBackM4y = QuatCast(m4y);
		if (!QuaternionsEpsilonEqual(qBackM4y, qY))
			TEST_FAILED("QuatCast(Mat4Cast(90 deg Y)) round-trip mismatch");

		// --- 45 deg X rotation matrix -> quaternion ---
		float angleX = Math::PI / 4.0f;
		Vector3 xAxis(1.0f, 0.0f, 0.0f);
		Math::Quaternion qX = AngleAxis(angleX, xAxis);

		Math::Mat3 m3x = Mat3Cast(qX);
		Math::Quaternion qBackM3x = QuatCast(m3x);
		if (!QuaternionsEpsilonEqual(qBackM3x, qX))
			TEST_FAILED("QuatCast(Mat3Cast(45 deg X)) round-trip mismatch");

		Math::Mat4 m4x = Mat4Cast(qX);
		Math::Quaternion qBackM4x = QuatCast(m4x);
		if (!QuaternionsEpsilonEqual(qBackM4x, qX))
			TEST_FAILED("QuatCast(Mat4Cast(45 deg X)) round-trip mismatch");

		// --- Arbitrary axis rotation matrix -> quaternion ---
		float arbAngle = Math::PI / 3.0f;
		Vector3 arbAxis(1.0f, 2.0f, 3.0f);
		arbAxis = Normalized(arbAxis);
		Math::Quaternion qArb = AngleAxis(arbAngle, arbAxis);

		Math::Mat3 m3Arb = Mat3Cast(qArb);
		Math::Quaternion qBackM3Arb = QuatCast(m3Arb);
		if (!QuaternionsEpsilonEqual(qBackM3Arb, qArb))
			TEST_FAILED("QuatCast(Mat3Cast(arbitrary axis)) round-trip mismatch");

		Math::Mat4 m4Arb = Mat4Cast(qArb);
		Math::Quaternion qBackM4Arb = QuatCast(m4Arb);
		if (!QuaternionsEpsilonEqual(qBackM4Arb, qArb))
			TEST_FAILED("QuatCast(Mat4Cast(arbitrary axis)) round-trip mismatch");

		// --- 180 deg rotation matrix -> quaternion (edge case: trace ~= -1) ---
		Math::Quaternion qZ180 = AngleAxis(Math::PI, Vector3(0.0f, 0.0f, 1.0f));

		Math::Mat3 m3z180 = Mat3Cast(qZ180);
		Math::Quaternion qBackM3z180 = QuatCast(m3z180);
		if (!QuaternionsEpsilonEqual(qBackM3z180, qZ180))
			TEST_FAILED("QuatCast(Mat3Cast(180 deg Z)) round-trip mismatch");

		Math::Mat4 m4z180 = Mat4Cast(qZ180);
		Math::Quaternion qBackM4z180 = QuatCast(m4z180);
		if (!QuaternionsEpsilonEqual(qBackM4z180, qZ180))
			TEST_FAILED("QuatCast(Mat4Cast(180 deg Z)) round-trip mismatch");

		// --- Euler-composed quaternion -> matrix -> quaternion ---
		float pitch = Math::PI / 6.0f;
		float yaw = Math::PI / 4.0f;
		float roll = Math::PI / 3.0f;
		Math::Quaternion qEuler = AngleAxis(yaw, Vector3(0.0f, 1.0f, 0.0f)) *
			AngleAxis(pitch, Vector3(1.0f, 0.0f, 0.0f)) *
			AngleAxis(roll, Vector3(0.0f, 0.0f, 1.0f));

		Math::Mat3 m3Euler = Mat3Cast(qEuler);
		Math::Quaternion qBackM3Euler = QuatCast(m3Euler);
		if (!QuaternionsEpsilonEqual(qBackM3Euler, qEuler))
			TEST_FAILED("QuatCast(Mat3Cast(euler composed)) round-trip mismatch");

		Math::Mat4 m4Euler = Mat4Cast(qEuler);
		Math::Quaternion qBackM4Euler = QuatCast(m4Euler);
		if (!QuaternionsEpsilonEqual(qBackM4Euler, qEuler))
			TEST_FAILED("QuatCast(Mat4Cast(euler composed)) round-trip mismatch");

		// --- Verify QuatCast(Mat4) extracts the same rotation as QuatCast(Mat3) from the same source ---
		Math::Mat4 m4Test = Mat4Cast(qArb);
		Math::Quaternion qFromM4 = QuatCast(m4Test);
		// Extract upper-left 3x3 manually
		Math::Mat3 m3Extracted{
			Vector3(m4Test.columns[0].x, m4Test.columns[0].y, m4Test.columns[0].z),
			Vector3(m4Test.columns[1].x, m4Test.columns[1].y, m4Test.columns[1].z),
			Vector3(m4Test.columns[2].x, m4Test.columns[2].y, m4Test.columns[2].z)
		};
		Math::Quaternion qFromM3 = QuatCast(m3Extracted);
		if (!QuaternionsEpsilonEqual(qFromM4, qFromM3))
			TEST_FAILED("QuatCast(Mat4) and QuatCast(Mat3) disagree on same rotation");

		// --- Verify that the extracted quaternion rotates vectors identically to the original quaternion ---
		Vector3 testVec(3.0f, -1.5f, 7.2f);
		Vector3 rotatedByOriginal = qArb.RotateVector(testVec);
		Vector3 rotatedByRoundTrip = qBackM4Arb.RotateVector(testVec);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedByOriginal), ValuePtr(rotatedByRoundTrip), 1e-5f))
			TEST_FAILED("Quaternion from Mat4Cast round-trip does not rotate vectors identically to original");

		// --- Verify that the round-tripped quaternion produces the same rotation matrix as the original ---
		Math::Mat3 m3RoundTrip = Mat3Cast(qBackM4Arb);
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(m3RoundTrip), ValuePtr(m3Arb), 1e-5f))
			TEST_FAILED("Mat3Cast(QuatCast(Mat3Cast(q))) does not reproduce original matrix");

		Math::Mat4 m4RoundTrip = Mat4Cast(qBackM4Arb);
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(m4RoundTrip), ValuePtr(m4Arb), 1e-5f))
			TEST_FAILED("Mat4Cast(QuatCast(Mat4Cast(q))) does not reproduce original matrix");

		TEST_PASSED();
	}
}
