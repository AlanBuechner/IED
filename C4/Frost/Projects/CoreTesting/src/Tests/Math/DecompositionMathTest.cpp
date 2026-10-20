#include "DecompositionMathTest.h"

namespace Testing
{
	using namespace Math;

	void DecompositionMathTest::TestDecomposeTransform()
	{
		Vector3 position(5.0f, -10.0f, 15.0f);
		Vector3 scale(2.0f, 3.0f, 4.0f);

		// Build a transform matrix from T * S (simple case: no rotation)
		Math::Mat4 transform = Translate(position) * Scale(scale);

		Vector3 outPos, outRot, outScale;
		bool success = DecomposeTransform(transform, outPos, outRot, outScale);

		if (!success)
			TEST_FAILED("DecomposeTransform returned false");

		if (!VectorsEpsilonEqual<3>(ValuePtr(outPos), ValuePtr(position)))
			TEST_FAILED("Decomposed position mismatch: got ({0}, {1}, {2}), expected ({3}, {4}, {5})",
				outPos.x, outPos.y, outPos.z, position.x, position.y, position.z);

		if (!VectorsEpsilonEqual<3>(ValuePtr(outScale), ValuePtr(scale)))
			TEST_FAILED("Decomposed scale mismatch: got ({0}, {1}, {2}), expected ({3}, {4}, {5})",
				outScale.x, outScale.y, outScale.z, scale.x, scale.y, scale.z);

		// Test with rotation (90 degrees around Y)
		float angle = Math::PI / 2.0f;
		Vector3 rotAxis(0.0f, 1.0f, 0.0f);
		Math::Mat4 transformWithRot = Translate(position) * Rotate(angle, rotAxis) * Scale(scale);

		success = DecomposeTransform(transformWithRot, outPos, outRot, outScale);
		if (!success)
			TEST_FAILED("DecomposeTransform with rotation returned false");

		if (!VectorsEpsilonEqual<3>(ValuePtr(outPos), ValuePtr(position)))
			TEST_FAILED("Decomposed position with rotation mismatch");

		if (!VectorsEpsilonEqual<3>(ValuePtr(outScale), ValuePtr(scale)))
			TEST_FAILED("Decomposed scale with rotation mismatch");

		// Rotation Y should be approximately -PI/2 (LH decomposition gives negated yaw for this composition order)
		if (!Math::EpsilonEqual(outRot.y, -angle, 1e-3f))
			TEST_FAILED("Decomposed rotation Y is {0}, expected {1}", outRot.y, -angle);

		// Test helper functions: GetTranslation and GetRotationScale
		Vector3 extractedTrans = GetTranslation(transformWithRot);
		if (!VectorsEpsilonEqual<3>(ValuePtr(extractedTrans), ValuePtr(position)))
			TEST_FAILED("GetTranslation mismatch");

		Math::Mat3 rotScale = GetRotationScale(transformWithRot);
		// Just verify the diagonal of rotation*scale matches scale for pure axis-aligned rotation
		Vector3 diag(
			Length(Vector3(rotScale.columns[0].x, rotScale.columns[0].y, rotScale.columns[0].z)),
			Length(Vector3(rotScale.columns[1].x, rotScale.columns[1].y, rotScale.columns[1].z)),
			Length(Vector3(rotScale.columns[2].x, rotScale.columns[2].y, rotScale.columns[2].z)));
		if (!VectorsEpsilonEqual<3>(ValuePtr(diag), ValuePtr(scale)))
			TEST_FAILED("GetRotationScale diagonal lengths don't match scale");

		// Test TransformMatrix helper (T * R * S)
		Vector3 eulerRot(0.0f, Math::PI / 4.0f, 0.0f);
		Vector3 tmScale(1.5f, 2.0f, 1.0f);
		Vector3 tmPos(10.0f, 5.0f, -3.0f);
		Math::Mat4 tm = TransformMatrix(tmPos, eulerRot, tmScale);

		// Verify by decomposing back
		Vector3 dPos, dRot, dScale;
		DecomposeTransform(tm, dPos, dRot, dScale);

		if (!VectorsEpsilonEqual<3>(ValuePtr(dPos), ValuePtr(tmPos)))
			TEST_FAILED("TransformMatrix position round-trip mismatch");

		if (!VectorsEpsilonEqual<3>(ValuePtr(dScale), ValuePtr(tmScale)))
			TEST_FAILED("TransformMatrix scale round-trip mismatch");

		TEST_PASSED();
	}
}
