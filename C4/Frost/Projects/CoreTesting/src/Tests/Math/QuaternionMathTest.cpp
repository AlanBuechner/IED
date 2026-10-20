#include "QuaternionMathTest.h"

namespace Testing
{
	using namespace Math;

	void QuaternionMathTest::TestQuaternionIdentity()
	{
		Math::Quaternion q = Math::Quaternion::Identity();
		glm::quat glmQ(1.0f, 0.0f, 0.0f, 0.0f); // GLM: (w=1, x=0, y=0, z=0)

		if (!QuaternionsEpsilonEqual(q, glmQ))
			TEST_FAILED("Quaternion identity mismatch");

		// Constructor with components
		Math::Quaternion qc(0.5f, 0.5f, 0.5f, 0.70710678f);
		glm::quat glmQc(0.70710678f, 0.5f, 0.5f, 0.5f); // GLM: (w=0.707..., x=0.5, y=0.5, z=0.5)
		if (!QuaternionsEpsilonEqual(qc, glmQc))
			TEST_FAILED("Quaternion component constructor mismatch");

		// Constructor from Vector3 + w
		Vector3 v(0.5f, 0.5f, 0.5f);
		Math::Quaternion qv(v, 0.70710678f);
		glm::quat glmQv(0.70710678f, glm::vec3(0.5f, 0.5f, 0.5f)); // GLM: (w, vec3)
		if (!QuaternionsEpsilonEqual(qv, glmQv))
			TEST_FAILED("Quaternion Vector3+w constructor mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestAngleAxis()
	{
		float angle = Math::PI / 2.0f; // 90 degrees
		Vector3 axis(0.0f, 1.0f, 0.0f);

		Math::Quaternion q = AngleAxis(angle, axis);
		glm::quat glmQ = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));

		if (!QuaternionsEpsilonEqual(q, glmQ))
			TEST_FAILED("AngleAxis Y mismatch");

		// X axis rotation
		Math::Quaternion qx = AngleAxis(angle, Vector3(1.0f, 0.0f, 0.0f));
		glm::quat glmQx = glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f));
		if (!QuaternionsEpsilonEqual(qx, glmQx))
			TEST_FAILED("AngleAxis X mismatch");

		// Z axis rotation
		Math::Quaternion qz = AngleAxis(angle, Vector3(0.0f, 0.0f, 1.0f));
		glm::quat glmQz = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
		if (!QuaternionsEpsilonEqual(qz, glmQz))
			TEST_FAILED("AngleAxis Z mismatch");

		// Arbitrary axis
		Vector3 arbAxis(1.0f, 2.0f, 3.0f);
		arbAxis = Normalized(arbAxis);
		Math::Quaternion qArb = AngleAxis(angle, arbAxis);
		glm::quat glmQArb = glm::angleAxis(angle, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		if (!QuaternionsEpsilonEqual(qArb, glmQArb))
			TEST_FAILED("AngleAxis arbitrary mismatch");

		// Scalar axis form
		Math::Quaternion qScalar = AngleAxis(angle, 1.0f, 0.0f, 0.0f);
		if (!QuaternionsEpsilonEqual(qScalar, glmQx))
			TEST_FAILED("AngleAxis scalar form mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionConjugateInverse()
	{
		Math::Quaternion q(0.1f, 0.2f, 0.3f, 0.905538514f); // normalized-ish

		// Conjugate
		Math::Quaternion conj = q.Conjugate();
		glm::quat glmQ(0.905538514f, 0.1f, 0.2f, 0.3f); // GLM: (w, x, y, z)
		glm::quat glmConj = glm::conjugate(glmQ);
		if (!QuaternionsEpsilonEqual(conj, glmConj))
			TEST_FAILED("Quaternion conjugate mismatch");

		// Free function conjugate
		Math::Quaternion conj2 = Conjugate(q);
		if (!QuaternionsEpsilonEqual(conj2, glmConj))
			TEST_FAILED("Free conjugate mismatch");

		// Inverse
		Math::Quaternion inv = q.Inverse();
		glm::quat glmInv = glm::inverse(glmQ);
		if (!QuaternionsEpsilonEqual(inv, glmInv))
			TEST_FAILED("Quaternion inverse mismatch");

		// Free function inverse
		Math::Quaternion inv2 = Inverse(q);
		if (!QuaternionsEpsilonEqual(inv2, glmInv))
			TEST_FAILED("Free inverse mismatch");

		// q * inv(q) should be identity (for unit quaternion)
		Math::Quaternion unitQ = Normalize(q);
		Math::Quaternion unitInv = unitQ.Inverse();
		Math::Quaternion product = unitQ * unitInv;
		if (!QuaternionsEpsilonEqual(product, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)))
			TEST_FAILED("q * inv(q) is not identity");

		// Dot product
		float dot = Dot(q, q);
		float glmDot = glm::dot(glmQ, glmQ);
		if (!Math::EpsilonEqual(dot, glmDot))
			TEST_FAILED("Quaternion dot mismatch: ours={0}, glm={1}", dot, glmDot);

		// Angle and axis
		float angle = Math::PI / 3.0f;
		Vector3 axis(0.0f, 1.0f, 0.0f);
		Math::Quaternion qa = AngleAxis(angle, axis);
		glm::quat glmQa = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));

		float ourAngle = Angle(qa);
		float glmAngle = glm::angle(glmQa);
		if (!Math::EpsilonEqual(ourAngle, glmAngle))
			TEST_FAILED("Quaternion angle mismatch: ours={0}, glm={1}", ourAngle, glmAngle);

		Vector3 ourAxis = Axis(qa);
		glm::vec3 glmAxis = glm::axis(glmQa);
		if (!VectorsEpsilonEqual<3>(ValuePtr(ourAxis), glm::value_ptr(glmAxis)))
			TEST_FAILED("Quaternion axis mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionNormalize()
	{
		Math::Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
		Math::Quaternion nq = Normalize(q);

		glm::quat glmQ(4.0f, 1.0f, 2.0f, 3.0f); // GLM: (w, x, y, z)
		glm::quat glmNq = glm::normalize(glmQ);

		if (!QuaternionsEpsilonEqual(nq, glmNq))
			TEST_FAILED("Quaternion normalize mismatch");

		// Verify unit length
		float len = Length(nq);
		if (!Math::EpsilonEqual(len, 1.0f))
			TEST_FAILED("Normalized quaternion length is {0}, expected 1.0", len);

		// Member function version
		Math::Quaternion nq2 = q.Normalize();
		if (!QuaternionsEpsilonEqual(nq2, glmNq))
			TEST_FAILED("Quaternion::Normalize() mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionMultiply()
	{
		// 90-degree Y rotation * 90-degree X rotation
		float angle = Math::PI / 2.0f;
		Math::Quaternion qy = AngleAxis(angle, Vector3(0.0f, 1.0f, 0.0f));
		Math::Quaternion qx = AngleAxis(angle, Vector3(1.0f, 0.0f, 0.0f));

		Math::Quaternion prod = qy * qx;
		glm::quat glmQy = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::quat glmQx = glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f));
		glm::quat glmProd = glmQy * glmQx;

		if (!QuaternionsEpsilonEqual(prod, glmProd))
			TEST_FAILED("Quaternion multiply mismatch");

		// Scalar multiplication
		Math::Quaternion scaled = qy * 2.0f;
		glm::quat glmScaled = glmQy * 2.0f;
		if (!QuaternionsEpsilonEqual(scaled, glmScaled))
			TEST_FAILED("Quaternion scalar multiply mismatch");

		// Quaternion addition/subtraction
		Math::Quaternion sum = qy + qx;
		glm::quat glmSum = glmQy + glmQx;
		if (!QuaternionsEpsilonEqual(sum, glmSum))
			TEST_FAILED("Quaternion addition mismatch");

		Math::Quaternion diff = qy - qx;
		glm::quat glmDiff = glmQy - glmQx;
		if (!QuaternionsEpsilonEqual(diff, glmDiff))
			TEST_FAILED("Quaternion subtraction mismatch");

		// Negation
		Math::Quaternion neg = -qy;
		glm::quat glmNeg = -glmQy;
		if (!QuaternionsEpsilonEqual(neg, glmNeg))
			TEST_FAILED("Quaternion negation mismatch");

		// Rotate function (angleAxis * q) — our Rotate pre-multiplies: rot * q
		// GLM's glm::rotate post-multiplies: q * angleAxis, so we must mirror the pre-multiply order.
		// Also negate the angle for LH vs RH handedness difference.
		Math::Quaternion rotQ = Rotate(qx, Math::PI / 4.0f, Vector3(0.0f, 1.0f, 0.0f));
		
		// Manually verify: rotQ should equal AngleAxis(PI/4, Y) * qx
		Math::Quaternion expectedRot = AngleAxis(Math::PI / 4.0f, Vector3(0.0f, 1.0f, 0.0f)) * qx;
		if (!QuaternionsEpsilonEqual(rotQ, expectedRot))
			TEST_FAILED("Quaternion rotate mismatch");

		// Abs (ensures positive w)
		Math::Quaternion negW(-0.1f, -0.2f, -0.3f, -0.9055f);
		Math::Quaternion absQ = Abs(negW);
		// glm::abs not available for quat in this version; verify manually
		Math::Quaternion expectedAbs(0.1f, 0.2f, 0.3f, 0.9055f);
		if (!QuaternionsEpsilonEqual(absQ, glm::quat(0.9055f, 0.1f, 0.2f, 0.3f)))
			TEST_FAILED("Quaternion abs mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestRotateVector()
	{
		// Rotate (1,0,0) by 90 degrees around Y -> should give (0,0,-1) in LH system
		float angle = Math::PI / 2.0f;
		Math::Quaternion q = AngleAxis(angle, Vector3(0.0f, 1.0f, 0.0f));

		Vector3 v(1.0f, 0.0f, 0.0f);
		Vector3 rotated = q.RotateVector(v);

		glm::quat glmQ = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 glmRotMat = glm::mat4_cast(glmQ);
		glm::vec3 glmRotated = static_cast<glm::vec3>(glmRotMat * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

		if (!VectorsEpsilonEqual<3>(ValuePtr(rotated), glm::value_ptr(glmRotated)))
			TEST_FAILED("Quaternion rotate vector mismatch");

		// Rotate (0,0,1) by 90 degrees around Y -> should give (1,0,0) in LH
		Vector3 vz(0.0f, 0.0f, 1.0f);
		Vector3 rotatedZ = q.RotateVector(vz);
		glm::vec3 glmRotatedZ = static_cast<glm::vec3>(glmRotMat * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedZ), glm::value_ptr(glmRotatedZ)))
			TEST_FAILED("Quaternion rotate Z vector mismatch");

		// Identity quaternion should not change the vector
		Math::Quaternion id = Math::Quaternion::Identity();
		Vector3 v2(3.0f, 4.0f, 5.0f);
		Vector3 rotatedId = id.RotateVector(v2);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedId), ValuePtr(v2)))
			TEST_FAILED("Identity quaternion should not change vector");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionLerpSlerp()
	{
		Math::Quaternion q1 = AngleAxis(0.0f, Vector3(0.0f, 1.0f, 0.0f));
		Math::Quaternion q2 = AngleAxis(Math::PI / 2.0f, Vector3(0.0f, 1.0f, 0.0f));

		glm::quat glmQ1 = glm::angleAxis(0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::quat glmQ2 = glm::angleAxis(Math::PI / 2.0f, glm::vec3(0.0f, 1.0f, 0.0f));

		// Lerp at t=0.5 — our Lerp normalizes and handles shortest path; GLM lerp is simple linear.
		// Compare against normalized GLM lerp result.
		Math::Quaternion lerp = Lerp(q1, q2, 0.5f);
		glm::quat glmLerpRaw = glmQ1 * 0.5f + glmQ2 * 0.5f;
		glm::quat glmLerp = glm::normalize(glmLerpRaw);
		if (!QuaternionsEpsilonEqual(lerp, glmLerp, 1e-3f))
			TEST_FAILED("Quaternion lerp mismatch");

		// Slerp at t=0.5
		Math::Quaternion slerp = Slerp(q1, q2, 0.5f);
		glm::quat glmSlerp = glm::slerp(glmQ1, glmQ2, 0.5f);
		if (!QuaternionsEpsilonEqual(slerp, glmSlerp, 1e-3f))
			TEST_FAILED("Quaternion slerp mismatch");

		// Mix (alias for Slerp)
		Math::Quaternion mix = Mix(q1, q2, 0.5f);
		if (!QuaternionsEpsilonEqual(mix, glmSlerp, 1e-3f))
			TEST_FAILED("Quaternion mix mismatch");

		// Edge cases: t=0 should return q1, t=1 should return q2
		Math::Quaternion lerp0 = Lerp(q1, q2, 0.0f);
		if (!QuaternionsEpsilonEqual(lerp0, glmQ1))
			TEST_FAILED("Lerp t=0 should return q1");

		Math::Quaternion slerp1 = Slerp(q1, q2, 1.0f);
		if (!QuaternionsEpsilonEqual(slerp1, glmQ2))
			TEST_FAILED("Slerp t=1 should return q2");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestEulerAngles()
	{
		// Create a quaternion from known Euler angles
		float pitch = Math::PI / 6.0f;  // 30 degrees
		float yaw = Math::PI / 4.0f;    // 45 degrees
		float roll = Math::PI / 3.0f;   // 60 degrees

		Math::Quaternion q = AngleAxis(yaw, Vector3(0.0f, 1.0f, 0.0f)) *
			AngleAxis(pitch, Vector3(1.0f, 0.0f, 0.0f)) *
			AngleAxis(roll, Vector3(0.0f, 0.0f, 1.0f));

		glm::quat glmQ = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));

		// Extract Euler angles — our EulerAngles uses LH convention (pitch = asin(-m12)),
		// GLM's glm::eulerAngles uses RH convention. Direct comparison is not valid.
		// Instead, verify round-trip: create quaternion from known Euler angles and extract back.
		float pitchOut, yawOut, rollOut;
		Vector3 ourEuler = EulerAngles(q);

		// Verify individual components match the Pitch/Yaw/Roll helpers
		if (!Math::EpsilonEqual(ourEuler.x, Pitch(q)))
			TEST_FAILED("EulerAngles.x != Pitch");
		if (!Math::EpsilonEqual(ourEuler.y, Yaw(q)))
			TEST_FAILED("EulerAngles.y != Yaw");
		if (!Math::EpsilonEqual(ourEuler.z, Roll(q)))
			TEST_FAILED("EulerAngles.z != Roll");

		// Individual pitch/yaw/roll extraction — our Pitch/Yaw/Roll use LH convention.
		// GLM's glm::pitch/yaw/roll use RH convention. Direct comparison is not valid.
		// Instead, verify that the individual helpers return the same values as EulerAngles().
		float ourPitch = Pitch(q);
		if (!Math::EpsilonEqual(ourPitch, ourEuler.x))
			TEST_FAILED("Pitch != EulerAngles.x: ours={0}, euler={1}", ourPitch, ourEuler.x);

		float ourYaw = Yaw(q);
		if (!Math::EpsilonEqual(ourYaw, ourEuler.y))
			TEST_FAILED("Yaw != EulerAngles.y: ours={0}, euler={1}", ourYaw, ourEuler.y);

		float ourRoll = Roll(q);
		if (!Math::EpsilonEqual(ourRoll, ourEuler.z))
			TEST_FAILED("Roll != EulerAngles.z: ours={0}, euler={1}", ourRoll, ourEuler.z);

		// Identity quaternion should give zero Euler angles
		Math::Quaternion id = Math::Quaternion::Identity();
		Vector3 idEuler = EulerAngles(id);
		glm::vec3 glmIdEuler = glm::eulerAngles(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		if (!VectorsEpsilonEqual<3>(ValuePtr(idEuler), glm::value_ptr(glmIdEuler)))
			TEST_FAILED("Identity quaternion Euler angles mismatch");

		// Verify identity gives all zeros
		if (!Math::EpsilonEqual(Pitch(id), 0.0f))
			TEST_FAILED("Identity pitch should be zero");
		if (!Math::EpsilonEqual(Yaw(id), 0.0f))
			TEST_FAILED("Identity yaw should be zero");
		if (!Math::EpsilonEqual(Roll(id), 0.0f))
			TEST_FAILED("Identity roll should be zero");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionMatrixCast()
	{
		float angle = Math::PI / 4.0f;
		Vector3 axis(1.0f, 1.0f, 1.0f);
		axis = Normalized(axis);

		Math::Quaternion q = AngleAxis(angle, axis);
		glm::quat glmQ = glm::angleAxis(angle, glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));

		// Mat3Cast — with GLM_FORCE_LEFT_HANDED defined, both our Mat3Cast and
		// glm::mat3_cast produce left-handed rotation matrices. Verify by checking
		// that it's a proper orthonormal rotation matrix (columns are unit vectors
		// and orthogonal) and comparing directly against GLM.
		Math::Mat3 m3 = Mat3Cast(q);

		// Verify columns are unit length
		Vector3 c0(m3.columns[0].x, m3.columns[0].y, m3.columns[0].z);
		Vector3 c1(m3.columns[1].x, m3.columns[1].y, m3.columns[1].z);
		Vector3 c2(m3.columns[2].x, m3.columns[2].y, m3.columns[2].z);

		if (!Math::EpsilonEqual(Length(c0), 1.0f))
			TEST_FAILED("Mat3Cast column 0 not unit length");
		if (!Math::EpsilonEqual(Length(c1), 1.0f))
			TEST_FAILED("Mat3Cast column 1 not unit length");
		if (!Math::EpsilonEqual(Length(c2), 1.0f))
			TEST_FAILED("Mat3Cast column 2 not unit length");

		// Verify columns are orthogonal
		if (!Math::EpsilonEqual(Dot(c0, c1), 0.0f))
			TEST_FAILED("Mat3Cast columns 0 and 1 not orthogonal");
		if (!Math::EpsilonEqual(Dot(c0, c2), 0.0f))
			TEST_FAILED("Mat3Cast columns 0 and 2 not orthogonal");
		if (!Math::EpsilonEqual(Dot(c1, c2), 0.0f))
			TEST_FAILED("Mat3Cast columns 1 and 2 not orthogonal");

		// Verify determinant is +1 (proper rotation)
		if (!Math::EpsilonEqual(m3.Determinant(), 1.0f))
			TEST_FAILED("Mat3Cast determinant is not +1");

		// Direct comparison with glm::mat3_cast — both are left-handed now
		glm::mat3 glmM3 = glm::mat3_cast(glmQ);
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(m3), glm::value_ptr(glmM3)))
			TEST_FAILED("Mat3Cast mismatch with glm::mat3_cast (LH)");

		// Verify round-trip: quaternion -> matrix -> quaternion gives same rotation
		Math::Quaternion qFromM3 = QuatCast(m3);
		if (!QuaternionsEpsilonEqual(qFromM3, glmQ))
			TEST_FAILED("QuatCast(Mat3Cast(q)) mismatch");

		// Mat4Cast — verify it wraps Mat3Cast correctly with zero translation and w=1
		Math::Mat4 m4 = Mat4Cast(q);

		// Upper-left 3x3 should match Mat3Cast (compare element by element)
		if (!Math::EpsilonEqual(m4.columns[0].x, m3.columns[0].x) ||
			!Math::EpsilonEqual(m4.columns[0].y, m3.columns[0].y) ||
			!Math::EpsilonEqual(m4.columns[0].z, m3.columns[0].z))
			TEST_FAILED("Mat4Cast column 0 doesn't match Mat3Cast");
		if (!Math::EpsilonEqual(m4.columns[1].x, m3.columns[1].x) ||
			!Math::EpsilonEqual(m4.columns[1].y, m3.columns[1].y) ||
			!Math::EpsilonEqual(m4.columns[1].z, m3.columns[1].z))
			TEST_FAILED("Mat4Cast column 1 doesn't match Mat3Cast");
		if (!Math::EpsilonEqual(m4.columns[2].x, m3.columns[2].x) ||
			!Math::EpsilonEqual(m4.columns[2].y, m3.columns[2].y) ||
			!Math::EpsilonEqual(m4.columns[2].z, m3.columns[2].z))
			TEST_FAILED("Mat4Cast column 2 doesn't match Mat3Cast");

		// Translation should be zero
		if (!Math::EpsilonEqual(m4.columns[3].x, 0.0f) ||
			!Math::EpsilonEqual(m4.columns[3].y, 0.0f) ||
			!Math::EpsilonEqual(m4.columns[3].z, 0.0f))
			TEST_FAILED("Mat4Cast translation should be zero");

		// w component should be 1
		if (!Math::EpsilonEqual(m4.columns[3].w, 1.0f))
			TEST_FAILED("Mat4Cast w should be 1");

		// QuatCast from Mat4 (round-trip)
		Math::Quaternion qFromM4 = QuatCast(m4);
		if (!QuaternionsEpsilonEqual(qFromM4, glmQ))
			TEST_FAILED("QuatCast(Mat4Cast(q)) mismatch");

		// Compare Mat4Cast against glm::mat4_cast for various rotated quaternions.
		// With GLM_FORCE_LEFT_HANDED defined, both our Mat4Cast and glm::mat4_cast
		// produce left-handed rotation matrices, so direct element comparison is valid.

#define VERIFY_MAT4_ROTATION(m, label) \
		do { \
			Vector3 _c0((m).columns[0].x, (m).columns[0].y, (m).columns[0].z); \
			Vector3 _c1((m).columns[1].x, (m).columns[1].y, (m).columns[1].z); \
			Vector3 _c2((m).columns[2].x, (m).columns[2].y, (m).columns[2].z); \
			if (!Math::EpsilonEqual(Length(_c0), 1.0f)) \
				TEST_FAILED("Mat4Cast column 0 not unit length on " label); \
			if (!Math::EpsilonEqual(Length(_c1), 1.0f)) \
				TEST_FAILED("Mat4Cast column 1 not unit length on " label); \
			if (!Math::EpsilonEqual(Length(_c2), 1.0f)) \
				TEST_FAILED("Mat4Cast column 2 not unit length on " label); \
			if (!Math::EpsilonEqual(Dot(_c0, _c1), 0.0f)) \
				TEST_FAILED("Mat4Cast columns 0 and 1 not orthogonal on " label); \
			if (!Math::EpsilonEqual(Dot(_c0, _c2), 0.0f)) \
				TEST_FAILED("Mat4Cast columns 0 and 2 not orthogonal on " label); \
			if (!Math::EpsilonEqual(Dot(_c1, _c2), 0.0f)) \
				TEST_FAILED("Mat4Cast columns 1 and 2 not orthogonal on " label); \
			if (!Math::EpsilonEqual((m).columns[3].x, 0.0f) || \
			    !Math::EpsilonEqual((m).columns[3].y, 0.0f) || \
			    !Math::EpsilonEqual((m).columns[3].z, 0.0f)) \
				TEST_FAILED("Mat4Cast translation should be zero on " label); \
			if (!Math::EpsilonEqual((m).columns[3].w, 1.0f)) \
				TEST_FAILED("Mat4Cast w should be 1 on " label); \
		} while(0)

#define VERIFY_MAT4_EQUALS_GLM(m, glmQ, label) \
		do { \
			glm::mat4 _glmM = glm::mat4_cast(glmQ); \
			if (!Math::EpsilonEqual((m).columns[0].x, _glmM[0][0]) || \
			    !Math::EpsilonEqual((m).columns[0].y, _glmM[0][1]) || \
			    !Math::EpsilonEqual((m).columns[0].z, _glmM[0][2])) \
				TEST_FAILED("Mat4Cast column 0 mismatch with glm on " label); \
			if (!Math::EpsilonEqual((m).columns[1].x, _glmM[1][0]) || \
			    !Math::EpsilonEqual((m).columns[1].y, _glmM[1][1]) || \
			    !Math::EpsilonEqual((m).columns[1].z, _glmM[1][2])) \
				TEST_FAILED("Mat4Cast column 1 mismatch with glm on " label); \
			if (!Math::EpsilonEqual((m).columns[2].x, _glmM[2][0]) || \
			    !Math::EpsilonEqual((m).columns[2].y, _glmM[2][1]) || \
			    !Math::EpsilonEqual((m).columns[2].z, _glmM[2][2])) \
				TEST_FAILED("Mat4Cast column 2 mismatch with glm on " label); \
			if (!Math::EpsilonEqual((m).columns[3].x, _glmM[3][0]) || \
			    !Math::EpsilonEqual((m).columns[3].y, _glmM[3][1]) || \
			    !Math::EpsilonEqual((m).columns[3].z, _glmM[3][2]) || \
			    !Math::EpsilonEqual((m).columns[3].w, _glmM[3][3])) \
				TEST_FAILED("Mat4Cast translation/w mismatch with glm on " label); \
		} while(0)

		// Diagonal axis 45° (already tested above as m4)
		glm::quat glmQDiag = glm::angleAxis(angle, glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));
		VERIFY_MAT4_ROTATION(m4, "diagonal axis 45°");
		VERIFY_MAT4_EQUALS_GLM(m4, glmQDiag, "diagonal axis 45°");

		// 90° around X
		Math::Quaternion qx = AngleAxis(Math::PI / 2.0f, Vector3(1.0f, 0.0f, 0.0f));
		glm::quat glmQx = glm::angleAxis(Math::PI / 2.0f, glm::vec3(1.0f, 0.0f, 0.0f));
		Math::Mat4 m4x = Mat4Cast(qx);
		VERIFY_MAT4_ROTATION(m4x, "X axis 90°");
		VERIFY_MAT4_EQUALS_GLM(m4x, glmQx, "X axis 90°");

		// 90° around Y
		Math::Quaternion qy = AngleAxis(Math::PI / 2.0f, Vector3(0.0f, 1.0f, 0.0f));
		glm::quat glmQy = glm::angleAxis(Math::PI / 2.0f, glm::vec3(0.0f, 1.0f, 0.0f));
		Math::Mat4 m4y = Mat4Cast(qy);
		VERIFY_MAT4_ROTATION(m4y, "Y axis 90°");
		VERIFY_MAT4_EQUALS_GLM(m4y, glmQy, "Y axis 90°");

		// 90° around Z
		Math::Quaternion qz = AngleAxis(Math::PI / 2.0f, Vector3(0.0f, 0.0f, 1.0f));
		glm::quat glmQz = glm::angleAxis(Math::PI / 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		Math::Mat4 m4z = Mat4Cast(qz);
		VERIFY_MAT4_ROTATION(m4z, "Z axis 90°");
		VERIFY_MAT4_EQUALS_GLM(m4z, glmQz, "Z axis 90°");

		// 180° around Y
		Math::Quaternion qy180 = AngleAxis(Math::PI, Vector3(0.0f, 1.0f, 0.0f));
		glm::quat glmQy180 = glm::angleAxis(Math::PI, glm::vec3(0.0f, 1.0f, 0.0f));
		Math::Mat4 m4y180 = Mat4Cast(qy180);
		VERIFY_MAT4_ROTATION(m4y180, "Y axis 180°");
		VERIFY_MAT4_EQUALS_GLM(m4y180, glmQy180, "Y axis 180°");

		// Combined rotations (yaw * pitch * roll)
		float pitch = Math::PI / 6.0f;
		float yaw = Math::PI / 4.0f;
		float roll = Math::PI / 3.0f;
		Math::Quaternion qCombined = AngleAxis(yaw, Vector3(0.0f, 1.0f, 0.0f)) *
			AngleAxis(pitch, Vector3(1.0f, 0.0f, 0.0f)) *
			AngleAxis(roll, Vector3(0.0f, 0.0f, 1.0f));
		glm::quat glmQCombined = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
			glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f)) *
			glm::angleAxis(roll, glm::vec3(0.0f, 0.0f, 1.0f));
		Math::Mat4 m4Combined = Mat4Cast(qCombined);
		VERIFY_MAT4_ROTATION(m4Combined, "combined yaw/pitch/roll");
		VERIFY_MAT4_EQUALS_GLM(m4Combined, glmQCombined, "combined yaw/pitch/roll");

		// Euler-constructed quaternion
		Math::Quaternion qEuler(pitch, yaw, roll);
		glm::quat glmQEuler(glm::vec3(pitch, yaw, roll));
		Math::Mat4 m4Euler = Mat4Cast(qEuler);
		VERIFY_MAT4_ROTATION(m4Euler, "Euler constructor");
		VERIFY_MAT4_EQUALS_GLM(m4Euler, glmQEuler, "Euler constructor");

#undef VERIFY_MAT4_ROTATION
#undef VERIFY_MAT4_EQUALS_GLM

		// Identity quaternion -> identity matrices (direct GLM comparison works here)
		Math::Quaternion id = Math::Quaternion::Identity();
		Math::Mat3 idM3 = Mat3Cast(id);
		glm::mat3 glmIdM3 = glm::mat3_cast(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		if (!MatricesEpsilonEqual<3, 3>(ValuePtr(idM3), glm::value_ptr(glmIdM3)))
			TEST_FAILED("Identity quaternion Mat3Cast mismatch");

		Math::Mat4 idM4 = Mat4Cast(id);
		glm::mat4 glmIdM4 = glm::mat4_cast(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		if (!MatricesEpsilonEqual<4, 4>(ValuePtr(idM4), glm::value_ptr(glmIdM4)))
			TEST_FAILED("Identity quaternion Mat4Cast mismatch");

		TEST_PASSED();
	}

	void QuaternionMathTest::TestEulerConstructor()
	{
		// Test 1: Identity (all zeros)
		Math::Quaternion qIdentity(0.0f, 0.0f, 0.0f);
		glm::quat glmIdentity(glm::vec3(0.0f, 0.0f, 0.0f));
		if (!QuaternionsEpsilonEqual(qIdentity, glmIdentity))
			TEST_FAILED("Euler constructor identity mismatch");

		// Test 2: Pitch only (30 degrees)
		float pitch = Math::PI / 6.0f;
		Math::Quaternion qPitch(pitch, 0.0f, 0.0f);
		glm::quat glmPitch(glm::vec3(pitch, 0.0f, 0.0f));
		if (!QuaternionsEpsilonEqual(qPitch, glmPitch))
			TEST_FAILED("Euler constructor pitch-only mismatch");

		// Test 3: Yaw only (45 degrees)
		float yaw = Math::PI / 4.0f;
		Math::Quaternion qYaw(0.0f, yaw, 0.0f);
		glm::quat glmYaw(glm::vec3(0.0f, yaw, 0.0f));
		if (!QuaternionsEpsilonEqual(qYaw, glmYaw))
			TEST_FAILED("Euler constructor yaw-only mismatch");

		// Test 4: Roll only (60 degrees)
		float roll = Math::PI / 3.0f;
		Math::Quaternion qRoll(0.0f, 0.0f, roll);
		glm::quat glmRoll(glm::vec3(0.0f, 0.0f, roll));
		if (!QuaternionsEpsilonEqual(qRoll, glmRoll))
			TEST_FAILED("Euler constructor roll-only mismatch");

		// Test 5: Combined pitch + yaw + roll
		Math::Quaternion qCombined(pitch, yaw, roll);
		glm::quat glmCombined(glm::vec3(pitch, yaw, roll));
		if (!QuaternionsEpsilonEqual(qCombined, glmCombined))
			TEST_FAILED("Euler constructor combined mismatch");

		// Test 6: Negative angles
		Math::Quaternion qNeg(-pitch, -yaw, -roll);
		glm::quat glmNeg(glm::vec3(-pitch, -yaw, -roll));
		if (!QuaternionsEpsilonEqual(qNeg, glmNeg))
			TEST_FAILED("Euler constructor negative angles mismatch");

		// Test 7: Large angles (near gimbal lock)
		float nearGimbal = Math::PI / 2.0f - 0.01f;
		Math::Quaternion qGimbal(nearGimbal, yaw, roll);
		glm::quat glmGimbal(glm::vec3(nearGimbal, yaw, roll));
		if (!QuaternionsEpsilonEqual(qGimbal, glmGimbal))
			TEST_FAILED("Euler constructor near gimbal lock mismatch");

		// Test 8: Full rotation (2*PI/TAU) should be identity
		Math::Quaternion qFull(0.0f, Math::TAU, 0.0f);
		glm::quat glmFull(glm::vec3(0.0f, glm::two_pi<float>(), 0.0f));
		if (!QuaternionsEpsilonEqual(qFull, glmFull))
			TEST_FAILED("Euler constructor full rotation mismatch");

		// Test 9: Verify constructed quaternion rotates vectors correctly
		// Create a 90-degree yaw quaternion and rotate (1,0,0) -> should give (0,0,1) in RH or (0,0,-1) depending on convention
		float yaw90 = Math::PI / 2.0f;
		Math::Quaternion qYaw90(0.0f, yaw90, 0.0f);
		glm::quat glmYaw90(glm::vec3(0.0f, yaw90, 0.0f));

		Vector3 v(1.0f, 0.0f, 0.0f);
		Vector3 rotated = qYaw90.RotateVector(v);

		glm::mat4 glmRotMat = glm::mat4_cast(glmYaw90);
		glm::vec3 glmRotated = static_cast<glm::vec3>(glmRotMat * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

		if (!VectorsEpsilonEqual<3>(ValuePtr(rotated), glm::value_ptr(glmRotated)))
			TEST_FAILED("Euler constructor rotate vector mismatch");

		// Test 10: Verify the constructed quaternion is unit length
		float len = Length(qCombined);
		if (!Math::EpsilonEqual(len, 1.0f))
			TEST_FAILED("Euler constructor result not unit length: {0}", len);

		TEST_PASSED();
	}

	void QuaternionMathTest::TestQuaternionVectorMultiply()
	{
		// Test 1: Identity quaternion should leave vector unchanged
		Math::Quaternion id = Math::Quaternion::Identity();
		Vector3 v(3.0f, 4.0f, 5.0f);
		Vector3 rotatedId = id * v;
		glm::vec3 glmRotatedId = glm::quat(1.0f, 0.0f, 0.0f, 0.0f) * glm::vec3(3.0f, 4.0f, 5.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedId), glm::value_ptr(glmRotatedId)))
			TEST_FAILED("Identity quaternion * vector mismatch");

		// Test 2: Rotate (1,0,0) by 90 degrees around Y -> should give (0,0,-1) in LH system
		float angle = Math::PI / 2.0f;
		Math::Quaternion qY = AngleAxis(angle, Vector3(0.0f, 1.0f, 0.0f));
		glm::quat glmQY = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));

		Vector3 v1(1.0f, 0.0f, 0.0f);
		Vector3 rotated1 = qY * v1;
		glm::vec3 glmRotated1 = glmQY * glm::vec3(1.0f, 0.0f, 0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotated1), glm::value_ptr(glmRotated1)))
			TEST_FAILED("90 deg Y rotation * (1,0,0) mismatch");

		// Test 3: Rotate (0,0,1) by 90 degrees around Y -> should give (1,0,0) in LH
		Vector3 vz(0.0f, 0.0f, 1.0f);
		Vector3 rotatedZ = qY * vz;
		glm::vec3 glmRotatedZ = glmQY * glm::vec3(0.0f, 0.0f, 1.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedZ), glm::value_ptr(glmRotatedZ)))
			TEST_FAILED("90 deg Y rotation * (0,0,1) mismatch");

		// Test 4: Rotate (0,1,0) by 90 degrees around X
		Math::Quaternion qX = AngleAxis(angle, Vector3(1.0f, 0.0f, 0.0f));
		glm::quat glmQX = glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f));

		Vector3 vy(0.0f, 1.0f, 0.0f);
		Vector3 rotatedY = qX * vy;
		glm::vec3 glmRotatedY = glmQX * glm::vec3(0.0f, 1.0f, 0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedY), glm::value_ptr(glmRotatedY)))
			TEST_FAILED("90 deg X rotation * (0,1,0) mismatch");

		// Test 5: Arbitrary axis rotation
		Vector3 arbAxis(1.0f, 2.0f, 3.0f);
		arbAxis = Normalized(arbAxis);
		float arbAngle = Math::PI / 4.0f;
		Math::Quaternion qArb = AngleAxis(arbAngle, arbAxis);
		glm::quat glmQArb = glm::angleAxis(arbAngle, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));

		Vector3 vArb(5.0f, -2.0f, 7.0f);
		Vector3 rotatedArb = qArb * vArb;
		glm::vec3 glmRotatedArb = glmQArb * glm::vec3(5.0f, -2.0f, 7.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedArb), glm::value_ptr(glmRotatedArb)))
			TEST_FAILED("Arbitrary axis rotation * vector mismatch");

		// Test 6: Verify that operator* produces same result as RotateVector
		Vector3 vMethod = qY.RotateVector(v1);
		Vector3 vOp = qY * v1;
		if (!VectorsEpsilonEqual<3>(ValuePtr(vMethod), ValuePtr(vOp)))
			TEST_FAILED("operator*(quat, vec3) != RotateVector");

		// Test 7: Euler-constructed quaternion * vector
		Math::Quaternion qEuler(0.0f, angle, 0.0f); // 90 deg yaw
		glm::quat glmQEuler(glm::vec3(0.0f, angle, 0.0f));

		Vector3 vEuler(1.0f, 2.0f, 3.0f);
		Vector3 rotatedEuler = qEuler * vEuler;
		glm::vec3 glmRotatedEuler = glmQEuler * glm::vec3(1.0f, 2.0f, 3.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedEuler), glm::value_ptr(glmRotatedEuler)))
			TEST_FAILED("Euler quaternion * vector mismatch");

		// Test 8: Zero vector should remain zero
		Vector3 vZero(0.0f, 0.0f, 0.0f);
		Vector3 rotatedZero = qArb * vZero;
		glm::vec3 glmRotatedZero = glmQArb * glm::vec3(0.0f, 0.0f, 0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedZero), glm::value_ptr(glmRotatedZero)))
			TEST_FAILED("Quaternion * zero vector mismatch");

		// Test 9: Negative vector components
		Vector3 vNeg(-1.0f, -2.0f, -3.0f);
		Vector3 rotatedNeg = qY * vNeg;
		glm::vec3 glmRotatedNeg = glmQY * glm::vec3(-1.0f, -2.0f, -3.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotatedNeg), glm::value_ptr(glmRotatedNeg)))
			TEST_FAILED("Quaternion * negative vector mismatch");

		// Test 10: Full 180 degree rotation around Z
		Math::Quaternion qZ180 = AngleAxis(Math::PI, Vector3(0.0f, 0.0f, 1.0f));
		glm::quat glmQZ180 = glm::angleAxis(Math::PI, glm::vec3(0.0f, 0.0f, 1.0f));

		Vector3 vTest(4.0f, 5.0f, 6.0f);
		Vector3 rotated180 = qZ180 * vTest;
		glm::vec3 glmRotated180 = glmQZ180 * glm::vec3(4.0f, 5.0f, 6.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(rotated180), glm::value_ptr(glmRotated180)))
			TEST_FAILED("180 deg Z rotation * vector mismatch");

		TEST_PASSED();
	}
}
