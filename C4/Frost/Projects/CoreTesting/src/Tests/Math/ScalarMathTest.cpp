#include "ScalarMathTest.h"

namespace Testing
{
	using namespace Math;

	void ScalarMathTest::TestScalarTrigonometry()
	{
		float angle = 1.234f;

		if (!Math::EpsilonEqual(Math::Sin(angle), glm::sin(angle)))
			TEST_FAILED("Sin mismatch: ours={0}, glm={1}", Math::Sin(angle), glm::sin(angle));

		if (!Math::EpsilonEqual(Math::Cos(angle), glm::cos(angle)))
			TEST_FAILED("Cos mismatch: ours={0}, glm={1}", Math::Cos(angle), glm::cos(angle));

		if (!Math::EpsilonEqual(Math::Tan(angle), glm::tan(angle)))
			TEST_FAILED("Tan mismatch: ours={0}, glm={1}", Math::Tan(angle), glm::tan(angle));

		if (!Math::EpsilonEqual(Math::Asin(0.5f), glm::asin(0.5f)))
			TEST_FAILED("Asin mismatch");

		if (!Math::EpsilonEqual(Math::Acos(0.5f), glm::acos(0.5f)))
			TEST_FAILED("Acos mismatch");

		if (!Math::EpsilonEqual(Math::Atan(0.5f), glm::atan(0.5f)))
			TEST_FAILED("Atan mismatch");

		if (!Math::EpsilonEqual(Math::Atan2(3.0f, 4.0f), glm::atan(3.0f, 4.0f)))
			TEST_FAILED("Atan2 mismatch");

		if (!Math::EpsilonEqual(Math::Sinh(angle), glm::sinh(angle)))
			TEST_FAILED("Sinh mismatch");

		if (!Math::EpsilonEqual(Math::Cosh(angle), glm::cosh(angle)))
			TEST_FAILED("Cosh mismatch");

		if (!Math::EpsilonEqual(Math::Tanh(angle), glm::tanh(angle)))
			TEST_FAILED("Tanh mismatch");

		TEST_PASSED();
	}

	void ScalarMathTest::TestScalarAngleConversion()
	{
		float deg = 180.0f;
		float rad = Math::PI;

		if (!Math::EpsilonEqual(Math::Radians(deg), glm::radians(deg)))
			TEST_FAILED("Radians mismatch: ours={0}, glm={1}", Math::Radians(deg), glm::radians(deg));

		if (!Math::EpsilonEqual(Math::Degrees(rad), glm::degrees(rad)))
			TEST_FAILED("Degrees mismatch: ours={0}, glm={1}", Math::Degrees(rad), glm::degrees(rad));

		// Round-trip
		float deg2 = Math::Degrees(Math::Radians(45.0f));
		if (!Math::EpsilonEqual(deg2, 45.0f))
			TEST_FAILED("Rad->Deg round-trip: got {0}, expected 45", deg2);

		float rad2 = Math::Radians(Math::Degrees(Math::PI / 4.0f));
		if (!Math::EpsilonEqual(rad2, Math::PI / 4.0f))
			TEST_FAILED("Deg->Rad round-trip");

		TEST_PASSED();
	}

	void ScalarMathTest::TestScalarExponentialLogarithmic()
	{
		if (!Math::EpsilonEqual(Math::Sqrt(2.0f), glm::sqrt(2.0f)))
			TEST_FAILED("Sqrt mismatch");

		if (!Math::EpsilonEqual(Math::InvSqrt(2.0f), glm::inversesqrt(2.0f), 1e-5f))
			TEST_FAILED("InvSqrt mismatch");

		if (!Math::EpsilonEqual(Math::Pow(2.0f, 10.0f), glm::pow(2.0f, 10.0f)))
			TEST_FAILED("Pow mismatch");

		if (!Math::EpsilonEqual(Math::Exp(1.0f), glm::exp(1.0f)))
			TEST_FAILED("Exp mismatch");

		if (!Math::EpsilonEqual(Math::Log(Math::E), glm::log(glm::e<float>()), 1e-5f))
			TEST_FAILED("Log(E) mismatch");

		TEST_PASSED();
	}

	void ScalarMathTest::TestScalarRounding()
	{
		float val = 3.7f;

		if (!Math::EpsilonEqual(Math::Floor(val), glm::floor(val)))
			TEST_FAILED("Floor mismatch");

		if (!Math::EpsilonEqual(Math::Ceil(val), glm::ceil(val)))
			TEST_FAILED("Ceil mismatch");

		if (!Math::EpsilonEqual(Math::Round(val), glm::round(val)))
			TEST_FAILED("Round mismatch");

		if (!Math::EpsilonEqual(Math::Fract(val), glm::fract(val)))
			TEST_FAILED("Fract mismatch");

		// Negative values
		float negVal = -2.3f;
		if (!Math::EpsilonEqual(Math::Floor(negVal), glm::floor(negVal)))
			TEST_FAILED("Floor negative mismatch");

		if (!Math::EpsilonEqual(Math::Ceil(negVal), glm::ceil(negVal)))
			TEST_FAILED("Ceil negative mismatch");

		TEST_PASSED();
	}

	void ScalarMathTest::TestScalarClampStepSmoothstep()
	{
		// Clamp
		if (!Math::EpsilonEqual(Math::Clamp(5.0f, 0.0f, 3.0f), glm::clamp(5.0f, 0.0f, 3.0f)))
			TEST_FAILED("Clamp high mismatch");

		if (!Math::EpsilonEqual(Math::Clamp(-2.0f, 0.0f, 3.0f), glm::clamp(-2.0f, 0.0f, 3.0f)))
			TEST_FAILED("Clamp low mismatch");

		if (!Math::EpsilonEqual(Math::Clamp(1.5f, 0.0f, 3.0f), glm::clamp(1.5f, 0.0f, 3.0f)))
			TEST_FAILED("Clamp in-range mismatch");

		// Step
		if (!Math::EpsilonEqual(Math::Step(2.0f, 1.5f), glm::step(2.0f, 1.5f)))
			TEST_FAILED("Step below edge mismatch");

		if (!Math::EpsilonEqual(Math::Step(2.0f, 2.5f), glm::step(2.0f, 2.5f)))
			TEST_FAILED("Step above edge mismatch");

		// SmoothStep
		float ss = Math::SmoothStep(0.0f, 1.0f, 0.5f);
		float glmSs = glm::smoothstep(0.0f, 1.0f, 0.5f);
		if (!Math::EpsilonEqual(ss, glmSs))
			TEST_FAILED("SmoothStep mismatch: ours={0}, glm={1}", ss, glmSs);

		// Min/Max
		if (!Math::EpsilonEqual(Math::Min(3.0f, 7.0f), glm::min(3.0f, 7.0f)))
			TEST_FAILED("Min mismatch");

		if (!Math::EpsilonEqual(Math::Max(3.0f, 7.0f), glm::max(3.0f, 7.0f)))
			TEST_FAILED("Max mismatch");

		// Abs
		if (!Math::EpsilonEqual(Math::Abs(-4.5f), glm::abs(-4.5f)))
			TEST_FAILED("Abs mismatch");

		TEST_PASSED();
	}

	void ScalarMathTest::TestScalarInterpolation()
	{
		float t = 0.25f;
		float a = 10.0f, b = 20.0f;

		if (!Math::EpsilonEqual(Math::Lerp(a, b, t), glm::mix(a, b, t)))
			TEST_FAILED("Scalar Lerp mismatch");

		// Edge cases
		if (!Math::EpsilonEqual(Math::Lerp(a, b, 0.0f), a))
			TEST_FAILED("Lerp t=0 should return a");

		if (!Math::EpsilonEqual(Math::Lerp(a, b, 1.0f), b))
			TEST_FAILED("Lerp t=1 should return b");

		TEST_PASSED();
	}
}
