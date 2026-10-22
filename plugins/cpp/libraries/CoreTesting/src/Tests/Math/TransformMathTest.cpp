#include "TransformMathTest.h"

namespace Testing
{
	using namespace Math;

	void TransformMathTest::TestTranslate()
	{
		Vector3 pos(5.0f, -10.0f, 15.0f);
		Math::Mat4 t = Translate(pos);
		glm::mat4 glmT = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, -10.0f, 15.0f));

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(t), glm::value_ptr(glmT)))
			TEST_FAILED("Translate mismatch");

		// Verify translation component
		if (!VectorsEpsilonEqual<3>(&t.columns[3].x, &pos.x))
			TEST_FAILED("Translate position not set correctly");

		// Upper-left 3x3 should be identity
		Math::Mat3 upper3(
			Vector3(t.columns[0].x, t.columns[0].y, t.columns[0].z),
			Vector3(t.columns[1].x, t.columns[1].y, t.columns[1].z),
			Vector3(t.columns[2].x, t.columns[2].y, t.columns[2].z));

		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(upper3), ValuePtr(Math::Mat3::Identity())))
			TEST_FAILED("Translate upper-left 3x3 not identity");

		TEST_PASSED();
	}

	void TransformMathTest::TestScale()
	{
		Vector3 s(2.0f, 3.0f, 4.0f);
		Math::Mat4 sc = Scale(s);
		glm::mat4 glmSc = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(sc), glm::value_ptr(glmSc)))
			TEST_FAILED("Scale mismatch");

		// Uniform scale
		Vector3 su(5.0f);
		Math::Mat4 scu = Scale(su);
		glm::mat4 glmScu = glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(scu), glm::value_ptr(glmScu)))
			TEST_FAILED("Uniform scale mismatch");

		TEST_PASSED();
	}

	void TransformMathTest::TestRotate()
	{
		float angle = Math::PI / 4.0f; // 45 degrees

		// Rotate around X axis - our Rotate is left-handed, GLM's rotate is right-handed.
		// LH(angle) == RH(-angle), so negate the angle for comparison.
		Math::Mat4 rotX = Rotate(angle, Vector3(1.0f, 0.0f, 0.0f));
		glm::mat4 glmRotX = glm::rotate(glm::mat4(1.0f), -angle, glm::vec3(1.0f, 0.0f, 0.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(rotX), glm::value_ptr(glmRotX)))
			TEST_FAILED("Rotate around X mismatch");

		// Rotate around Y axis
		Math::Mat4 rotY = Rotate(angle, Vector3(0.0f, 1.0f, 0.0f));
		glm::mat4 glmRotY = glm::rotate(glm::mat4(1.0f), -angle, glm::vec3(0.0f, 1.0f, 0.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(rotY), glm::value_ptr(glmRotY)))
			TEST_FAILED("Rotate around Y mismatch");

		// Rotate around Z axis
		Math::Mat4 rotZ = Rotate(angle, Vector3(0.0f, 0.0f, 1.0f));
		glm::mat4 glmRotZ = glm::rotate(glm::mat4(1.0f), -angle, glm::vec3(0.0f, 0.0f, 1.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(rotZ), glm::value_ptr(glmRotZ)))
			TEST_FAILED("Rotate around Z mismatch");

		// Rotate around arbitrary axis
		Vector3 axis(1.0f, 1.0f, 1.0f);
		axis = Normalized(axis);
		Math::Mat4 rotArb = Rotate(angle, axis);
		glm::mat4 glmRotArb = glm::rotate(glm::mat4(1.0f), -angle, glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(rotArb), glm::value_ptr(glmRotArb)))
			TEST_FAILED("Rotate around arbitrary axis mismatch");

		// Rotate with zero axis should return identity
		Math::Mat4 rotZero = Rotate(angle, Vector3(0.0f, 0.0f, 0.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(rotZero), ValuePtr(Math::Mat4::Identity())))
			TEST_FAILED("Rotate with zero axis should return identity");

		// EulerAngleYXZ composition - our Rotate is LH, so negate angles for GLM comparison
		float pitch = Math::PI / 6.0f;
		float yaw = Math::PI / 3.0f;
		float roll = Math::PI / 4.0f;
		Math::Mat4 euler = EulerAngleYXZ(pitch, yaw, roll);
		glm::mat4 glmEuler = glm::rotate(glm::mat4(1.0f), -yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), -pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), -roll, glm::vec3(0.0f, 0.0f, 1.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(euler), glm::value_ptr(glmEuler)))
			TEST_FAILED("EulerAngleYXZ mismatch");

		TEST_PASSED();
	}

	void TransformMathTest::TestOrtho()
	{
		float left = -10.0f, right = 10.0f;
		float bottom = -5.0f, top = 5.0f;
		float nearZ = 1.0f, farZ = 100.0f;

		Math::Mat4 ortho = Ortho(left, right, bottom, top, nearZ, farZ);
		glm::mat4 glmOrtho = glm::orthoLH_ZO(left, right, bottom, top, nearZ, farZ);

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(ortho), glm::value_ptr(glmOrtho)))
			TEST_FAILED("Ortho LH ZO mismatch");

		TEST_PASSED();
	}

	void TransformMathTest::TestPerspective()
	{
		float fovy = Math::PI / 4.0f; // 45 degrees
		float aspect = 16.0f / 9.0f;
		float nearZ = 0.1f, farZ = 1000.0f;

		Math::Mat4 persp = Perspective(fovy, aspect, nearZ, farZ);
		glm::mat4 glmPersp = glm::perspectiveLH_ZO(fovy, aspect, nearZ, farZ);

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(persp), glm::value_ptr(glmPersp)))
			TEST_FAILED("Perspective LH ZO mismatch");

		// Verify key properties: diagonal elements should be non-zero
		if (persp.columns[0].x == 0.0f || persp.columns[1].y == 0.0f)
			TEST_FAILED("Perspective diagonal elements are zero");

		// z-range mapping: near plane maps to 0, far plane maps to 1 in NDC
		if (!Math::EpsilonEqual(persp.columns[3].w, 0.0f))
			TEST_FAILED("Perspective w-row should be 0 for LH_ZO");

		if (persp.columns[2].w != 1.0f)
			TEST_FAILED("Perspective z-w component should be 1 for LH_ZO");

		TEST_PASSED();
	}

	void TransformMathTest::TestLookAt()
	{
		Vector3 eye(0.0f, 5.0f, 10.0f);
		Vector3 center(0.0f, 0.0f, 0.0f);
		Vector3 up(0.0f, 1.0f, 0.0f);

		Math::Mat4 lookAt = LookAt(eye, center, up);
		glm::mat4 glmLookAt = glm::lookAtLH(glm::vec3(0.0f, 5.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(lookAt), glm::value_ptr(glmLookAt)))
			TEST_FAILED("LookAt LH mismatch");

		// Test with different positions
		Vector3 eye2(5.0f, 3.0f, -8.0f);
		Vector3 center2(1.0f, 2.0f, -3.0f);
		Math::Mat4 lookAt2 = LookAt(eye2, center2, up);
		glm::mat4 glmLookAt2 = glm::lookAtLH(glm::vec3(5.0f, 3.0f, -8.0f),
			glm::vec3(1.0f, 2.0f, -3.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(lookAt2), glm::value_ptr(glmLookAt2)))
			TEST_FAILED("LookAt LH second test mismatch");

		TEST_PASSED();
	}
}
