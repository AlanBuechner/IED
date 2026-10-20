#include "VectorMathTest.h"

namespace Testing
{
	using namespace Math;

	void VectorMathTest::TestVectorConstructors()
	{
		// Default constructor
		Math::Vector3 v0;
		glm::vec3 glmV0(0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(v0), glm::value_ptr(glmV0)))
			TEST_FAILED("Vector3 default constructor mismatch");

		// Component constructor
		Math::Vector3 v1(1.0f, 2.0f, 3.0f);
		glm::vec3 glmV1(1.0f, 2.0f, 3.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(v1), glm::value_ptr(glmV1)))
			TEST_FAILED("Vector3 component constructor mismatch");

		// Swizzle construction from Vector2 + scalar
		Math::Vector2 v2(5.0f, 6.0f);
		Math::Vector3 v23(v2, 7.0f);
		glm::vec3 glmV23(glm::vec2(5.0f, 6.0f), 7.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(v23), glm::value_ptr(glmV23)))
			TEST_FAILED("Vector3 from Vector2+scalar mismatch");

		// Vector4 construction
		Math::Vector4 v4(1.0f, 2.0f, 3.0f, 4.0f);
		glm::vec4 glmV4(1.0f, 2.0f, 3.0f, 4.0f);
		if (!VectorsEpsilonEqual<4>(ValuePtr(v4), glm::value_ptr(glmV4)))
			TEST_FAILED("Vector4 component constructor mismatch");

		// Static constants
		Vector3F zero = Vector3F::Zero();
		glm::vec3 glmZero(0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(zero), glm::value_ptr(glmZero)))
			TEST_FAILED("Vector3::Zero mismatch");

		Vector3F unitX = Vector3F::UnitX();
		glm::vec3 glmUnitX(1.0f, 0.0f, 0.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(unitX), glm::value_ptr(glmUnitX)))
			TEST_FAILED("Vector3::UnitX mismatch");

		TEST_PASSED();
	}

	void VectorMathTest::TestVectorArithmetic()
	{
		Math::Vector3 a(1.0f, 2.0f, 3.0f);
		Math::Vector3 b(4.0f, 5.0f, 6.0f);

		glm::vec3 glmA(1.0f, 2.0f, 3.0f);
		glm::vec3 glmB(4.0f, 5.0f, 6.0f);

		// Addition
		Math::Vector3 sum = a + b;
		glm::vec3 glmSum = glmA + glmB;
		if (!VectorsEpsilonEqual<3>(ValuePtr(sum), glm::value_ptr(glmSum)))
			TEST_FAILED("Vector3 addition mismatch");

		// Subtraction
		Math::Vector3 diff = a - b;
		glm::vec3 glmDiff = glmA - glmB;
		if (!VectorsEpsilonEqual<3>(ValuePtr(diff), glm::value_ptr(glmDiff)))
			TEST_FAILED("Vector3 subtraction mismatch");

		// Scalar multiplication
		Math::Vector3 scaled = a * 2.0f;
		glm::vec3 glmScaled = glmA * 2.0f;
		if (!VectorsEpsilonEqual<3>(ValuePtr(scaled), glm::value_ptr(glmScaled)))
			TEST_FAILED("Vector3 scalar multiply mismatch");

		// Scalar multiplication (reverse)
		Math::Vector3 scaledRev = 2.0f * a;
		glm::vec3 glmScaledRev = 2.0f * glmA;
		if (!VectorsEpsilonEqual<3>(ValuePtr(scaledRev), glm::value_ptr(glmScaledRev)))
			TEST_FAILED("Vector3 reverse scalar multiply mismatch");

		// Component-wise multiplication
		Math::Vector3 compMul = a * b;
		glm::vec3 glmCompMul = glmA * glmB;
		if (!VectorsEpsilonEqual<3>(ValuePtr(compMul), glm::value_ptr(glmCompMul)))
			TEST_FAILED("Vector3 component-wise multiply mismatch");

		// Negation
		Math::Vector3 neg = -a;
		glm::vec3 glmNeg = -glmA;
		if (!VectorsEpsilonEqual<3>(ValuePtr(neg), glm::value_ptr(glmNeg)))
			TEST_FAILED("Vector3 negation mismatch");

		// Compound assignment
		Math::Vector3 c(1.0f, 1.0f, 1.0f);
		c += b;
		glm::vec3 glmC(1.0f, 1.0f, 1.0f);
		glmC += glmB;
		if (!VectorsEpsilonEqual<3>(ValuePtr(c), glm::value_ptr(glmC)))
			TEST_FAILED("Vector3 += mismatch");

		TEST_PASSED();
	}

	void VectorMathTest::TestDotProduct()
	{
		Math::Vector2 v2a(1.0f, 2.0f);
		Math::Vector2 v2b(3.0f, 4.0f);
		float dot2 = Dot(v2a, v2b);
		float glmDot2 = glm::dot(glm::vec2(1.0f, 2.0f), glm::vec2(3.0f, 4.0f));
		if (!Math::EpsilonEqual(dot2, glmDot2))
			TEST_FAILED("Vector2 dot mismatch: ours={0}, glm={1}", dot2, glmDot2);

		Math::Vector3 v3a(1.0f, 2.0f, 3.0f);
		Math::Vector3 v3b(4.0f, 5.0f, 6.0f);
		float dot3 = Dot(v3a, v3b);
		float glmDot3 = glm::dot(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(4.0f, 5.0f, 6.0f));
		if (!Math::EpsilonEqual(dot3, glmDot3))
			TEST_FAILED("Vector3 dot mismatch: ours={0}, glm={1}", dot3, glmDot3);

		Math::Vector4 v4a(1.0f, 2.0f, 3.0f, 4.0f);
		Math::Vector4 v4b(5.0f, 6.0f, 7.0f, 8.0f);
		float dot4 = Dot(v4a, v4b);
		float glmDot4 = glm::dot(glm::vec4(1.0f, 2.0f, 3.0f, 4.0f), glm::vec4(5.0f, 6.0f, 7.0f, 8.0f));
		if (!Math::EpsilonEqual(dot4, glmDot4))
			TEST_FAILED("Vector4 dot mismatch: ours={0}, glm={1}", dot4, glmDot4);

		// Orthogonal vectors should have zero dot product
		Math::Vector3 ux(1.0f, 0.0f, 0.0f);
		Math::Vector3 uy(0.0f, 1.0f, 0.0f);
		if (!Math::EpsilonEqual(Dot(ux, uy), 0.0f))
			TEST_FAILED("Orthogonal vectors dot product should be zero");

		TEST_PASSED();
	}

	void VectorMathTest::TestCrossProduct()
	{
		Math::Vector3 a(1.0f, 0.0f, 0.0f);
		Math::Vector3 b(0.0f, 1.0f, 0.0f);
		Math::Vector3 c = Cross(a, b);
		glm::vec3 glmC = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		if (!VectorsEpsilonEqual<3>(ValuePtr(c), glm::value_ptr(glmC)))
			TEST_FAILED("Cross product X x Y mismatch");

		// Y x Z = X
		Math::Vector3 y(0.0f, 1.0f, 0.0f);
		Math::Vector3 z(0.0f, 0.0f, 1.0f);
		Math::Vector3 cyz = Cross(y, z);
		glm::vec3 glmCyz = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		if (!VectorsEpsilonEqual<3>(ValuePtr(cyz), glm::value_ptr(glmCyz)))
			TEST_FAILED("Cross product Y x Z mismatch");

		// Anti-commutativity: a x b = -(b x a)
		Math::Vector3 p(2.0f, 3.0f, 5.0f);
		Math::Vector3 q(7.0f, 1.0f, 4.0f);
		Math::Vector3 cpq = Cross(p, q);
		Math::Vector3 cqp = Cross(q, p);
		Math::Vector3 negCpq = -cpq;
		if (!VectorsEpsilonEqual<3>(ValuePtr(cqp), ValuePtr(negCpq)))
			TEST_FAILED("Cross product anti-commutativity failed");

		TEST_PASSED();
	}

	void VectorMathTest::TestLengthAndDistance()
	{
		Math::Vector3 v(3.0f, 4.0f, 0.0f);
		float len = Length(v);
		float glmLen = glm::length(glm::vec3(3.0f, 4.0f, 0.0f));
		if (!Math::EpsilonEqual(len, glmLen))
			TEST_FAILED("Length mismatch: ours={0}, glm={1}", len, glmLen);

		float lenSqr = LengthSqr(v);
		float glmLenSqr = glm::dot(glm::vec3(3.0f, 4.0f, 0.0f), glm::vec3(3.0f, 4.0f, 0.0f));
		if (!Math::EpsilonEqual(lenSqr, glmLenSqr))
			TEST_FAILED("LengthSq mismatch: ours={0}, glm={1}", lenSqr, glmLenSqr);

		// Distance
		Math::Vector3 a(0.0f, 0.0f, 0.0f);
		Math::Vector3 b(3.0f, 4.0f, 0.0f);
		float dist = Distance(a, b);
		float glmDist = glm::distance(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(3.0f, 4.0f, 0.0f));
		if (!Math::EpsilonEqual(dist, glmDist))
			TEST_FAILED("Distance mismatch: ours={0}, glm={1}", dist, glmDist);

		// Vector2 and Vector4 length
		Math::Vector2 v2(3.0f, 4.0f);
		if (!Math::EpsilonEqual(Length(v2), glm::length(glm::vec2(3.0f, 4.0f))))
			TEST_FAILED("Vector2 length mismatch");

		Math::Vector4 v4(1.0f, 2.0f, 2.0f, 4.0f);
		if (!Math::EpsilonEqual(Length(v4), glm::length(glm::vec4(1.0f, 2.0f, 2.0f, 4.0f))))
			TEST_FAILED("Vector4 length mismatch");

		TEST_PASSED();
	}

	void VectorMathTest::TestNormalize()
	{
		Math::Vector3 v(3.0f, 4.0f, 0.0f);
		Math::Vector3 n = Normalized(v);
		glm::vec3 glmN = glm::normalize(glm::vec3(3.0f, 4.0f, 0.0f));

		if (!VectorsEpsilonEqual<3>(ValuePtr(n), glm::value_ptr(glmN)))
			TEST_FAILED("Vector3 normalize mismatch");

		// Normalized vector should have unit length
		float nLen = Length(n);
		if (!Math::EpsilonEqual(nLen, 1.0f))
			TEST_FAILED("Normalized vector length is {0}, expected 1.0", nLen);

		// Vector2 normalize
		Math::Vector2 v2(1.0f, 1.0f);
		Math::Vector2 n2 = Normalized(v2);
		glm::vec2 glmN2 = glm::normalize(glm::vec2(1.0f, 1.0f));
		if (!VectorsEpsilonEqual<2>(ValuePtr(n2), glm::value_ptr(glmN2)))
			TEST_FAILED("Vector2 normalize mismatch");

		// Vector4 normalize
		Math::Vector4 v4(1.0f, 0.0f, 0.0f, 0.0f);
		Math::Vector4 n4 = Normalized(v4);
		glm::vec4 glmN4 = glm::normalize(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
		if (!VectorsEpsilonEqual<4>(ValuePtr(n4), glm::value_ptr(glmN4)))
			TEST_FAILED("Vector4 normalize mismatch");

		TEST_PASSED();
	}

	void VectorMathTest::TestVectorLerpClampMinMax()
	{
		Math::Vector3 a(0.0f, 0.0f, 0.0f);
		Math::Vector3 b(10.0f, 20.0f, 30.0f);

		glm::vec3 glmA(0.0f, 0.0f, 0.0f);
		glm::vec3 glmB(10.0f, 20.0f, 30.0f);

		// Lerp
		Math::Vector3 lerp = Lerp(a, b, 0.5f);
		glm::vec3 glmLerp = glm::mix(glmA, glmB, 0.5f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(lerp), glm::value_ptr(glmLerp)))
			TEST_FAILED("Vector3 lerp mismatch");

		// Clamp
		Math::Vector3 v(15.0f, -5.0f, 10.0f);
		Math::Vector3 clamped = Clamp(v, 0.0f, 10.0f);
		glm::vec3 glmClamped = glm::clamp(glm::vec3(15.0f, -5.0f, 10.0f), 0.0f, 10.0f);
		if (!VectorsEpsilonEqual<3>(ValuePtr(clamped), glm::value_ptr(glmClamped)))
			TEST_FAILED("Vector3 clamp mismatch");

		// Min
		Math::Vector3 lo(1.0f, 5.0f, 3.0f);
		Math::Vector3 hi(4.0f, 2.0f, 6.0f);
		Math::Vector3 minV = Min(lo, hi);
		glm::vec3 glmMin = glm::min(glm::vec3(1.0f, 5.0f, 3.0f), glm::vec3(4.0f, 2.0f, 6.0f));
		if (!VectorsEpsilonEqual<3>(ValuePtr(minV), glm::value_ptr(glmMin)))
			TEST_FAILED("Vector3 min mismatch");

		// Max
		Math::Vector3 maxV = Max(lo, hi);
		glm::vec3 glmMax = glm::max(glm::vec3(1.0f, 5.0f, 3.0f), glm::vec3(4.0f, 2.0f, 6.0f));
		if (!VectorsEpsilonEqual<3>(ValuePtr(maxV), glm::value_ptr(glmMax)))
			TEST_FAILED("Vector3 max mismatch");

		// Degrees / Radians on vectors
		Math::Vector3 rad(0.0f, Math::PI / 2.0f, Math::PI);
		Math::Vector3 deg = Degrees(rad);
		glm::vec3 glmDeg = glm::degrees(glm::vec3(0.0f, Math::PI / 2.0f, Math::PI));
		if (!VectorsEpsilonEqual<3>(ValuePtr(deg), glm::value_ptr(glmDeg)))
			TEST_FAILED("Vector3 degrees mismatch");

		Math::Vector3 degBack = Radians(deg);
		glm::vec3 glmRadBack = glm::radians(glmDeg);
		if (!VectorsEpsilonEqual<3>(ValuePtr(degBack), glm::value_ptr(glmRadBack)))
			TEST_FAILED("Vector3 radians mismatch");

		TEST_PASSED();
	}

	void VectorMathTest::TestVectorAbsFloorCeil()
	{
		Math::Vector3 v(-1.5f, 2.7f, -3.2f);
		glm::vec3 glmV(-1.5f, 2.7f, -3.2f);

		// Abs
		Math::Vector3 absV = Abs(v);
		glm::vec3 glmAbs = glm::abs(glmV);
		if (!VectorsEpsilonEqual<3>(ValuePtr(absV), glm::value_ptr(glmAbs)))
			TEST_FAILED("Vector3 abs mismatch");

		// Floor
		Math::Vector3 floorV = Floor(v);
		glm::vec3 glmFloor = glm::floor(glmV);
		if (!VectorsEpsilonEqual<3>(ValuePtr(floorV), glm::value_ptr(glmFloor)))
			TEST_FAILED("Vector3 floor mismatch");

		// Ceil
		Math::Vector3 ceilV = Ceil(v);
		glm::vec3 glmCeil = glm::ceil(glmV);
		if (!VectorsEpsilonEqual<3>(ValuePtr(ceilV), glm::value_ptr(glmCeil)))
			TEST_FAILED("Vector3 ceil mismatch");

		// Vector2 and Vector4 variants
		Math::Vector2 v2(-1.5f, 2.7f);
		Math::Vector2 abs2 = Abs(v2);
		glm::vec2 glmAbs2 = glm::abs(glm::vec2(-1.5f, 2.7f));
		if (!VectorsEpsilonEqual<2>(ValuePtr(abs2), glm::value_ptr(glmAbs2)))
			TEST_FAILED("Vector2 abs mismatch");

		Math::Vector4 v4(-1.5f, 2.7f, -3.2f, 4.8f);
		Math::Vector4 floor4 = Floor(v4);
		glm::vec4 glmFloor4 = glm::floor(glm::vec4(-1.5f, 2.7f, -3.2f, 4.8f));
		if (!VectorsEpsilonEqual<4>(ValuePtr(floor4), glm::value_ptr(glmFloor4)))
			TEST_FAILED("Vector4 floor mismatch");

		TEST_PASSED();
	}
}
