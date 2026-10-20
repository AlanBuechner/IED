// Math library benchmark - C++ side: math.cf over GLM (left-handed, depth 0 to 1, like Bedrock Math).
// Built twice by run-benchmark.ps1: plain GLM, and GLM with its SSE paths (intrinsics over aligned types).
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include "../glm/glm/glm.hpp"
#include "../glm/glm/gtc/matrix_transform.hpp"
#include "../glm/glm/gtc/quaternion.hpp"
#include "../glm/glm/gtx/euler_angles.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

static const int kCount = 4096;
static const int kMask = kCount - 1;
static const int kRepeats = 400;
static const float kPi = 3.14159265358979323846f;

struct Scene {
	glm::vec3* positions;
	glm::vec3* angles;
	glm::vec3* scales;
	glm::quat* rotations;
	glm::mat4* matrices;
};

struct Outputs {
	glm::mat4* matrices;
	glm::vec3* vectors;
	glm::quat* rotations;
};

// --- the scene ---------------------------------------------------------------------------------------------

static float Wave(int i, int lane) { return std::sin((float)(i * 7 + lane * 13) * 0.01f); }

static glm::vec3 WaveVector(int i, int lane) { return glm::vec3(Wave(i, lane), Wave(i, lane + 1), Wave(i, lane + 2)); }

// Bedrock's TransformMatrix: translate * (yaw * pitch * roll * scale), Euler angles as (pitch, yaw, roll).
static glm::mat4 TransformMatrix(glm::vec3 position, glm::vec3 angles, glm::vec3 scale) {
	return glm::translate(glm::mat4(1.0f), position) * (glm::eulerAngleYXZ(angles.y, angles.x, angles.z) * glm::scale(glm::mat4(1.0f), scale));
}

static void FillScene(Scene& scene) {
	for (int i = 0; i < kCount; ++i) {
		scene.positions[i] = WaveVector(i, 0) * 10.0f;
		scene.angles[i] = WaveVector(i, 3) * kPi;
		scene.scales[i] = WaveVector(i, 6) * 0.5f + glm::vec3(1.0f);
		scene.rotations[i] = glm::angleAxis(Wave(i, 9) * kPi, glm::normalize(WaveVector(i, 10) + glm::vec3(0.0f, 2.0f, 0.0f)));
		scene.matrices[i] = TransformMatrix(scene.positions[i], scene.angles[i], scene.scales[i]);
	}
}

// --- the kernels ---------------------------------------------------------------------------------------------

static void BuildTransforms(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) {
		int j = (i + shift) & kMask;
		out.matrices[i] = TransformMatrix(scene.positions[j], scene.angles[i], scene.scales[i]);
	}
}

static void MultiplyMatrices(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.matrices[i] = scene.matrices[i] * scene.matrices[(i + shift) & kMask];
}

static void InvertMatrices(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.matrices[i] = glm::inverse(scene.matrices[(i + shift) & kMask]);
}

static void TransformPoints(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.vectors[i] = glm::vec3(scene.matrices[(i + shift) & 63] * glm::vec4(scene.positions[i], 1.0f));
}

static void RotateVectors(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.vectors[i] = scene.rotations[(i + shift) & kMask] * scene.positions[i];
}

static void SlerpRotations(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.rotations[i] = glm::slerp(scene.rotations[i], scene.rotations[(i + shift + 1) & kMask], 0.3f);
}

static void NormalizeCrosses(const Scene& scene, Outputs& out, int shift) {
	for (int i = 0; i < kCount; ++i) out.vectors[i] = glm::normalize(glm::cross(scene.positions[i], scene.angles[(i + shift) & kMask]));
}

// --- checksums -----------------------------------------------------------------------------------------------

static double MatrixSum(const Outputs& out) {
	double sum = 0.0;
	for (int i = 0; i < kCount; ++i)
		for (int c = 0; c < 4; ++c) sum += (double)glm::dot(out.matrices[i][c], glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
	return sum;
}

static double VectorSum(const Outputs& out) {
	double sum = 0.0;
	for (int i = 0; i < kCount; ++i) sum += (double)glm::dot(out.vectors[i], glm::vec3(1.0f, 2.0f, 3.0f));
	return sum;
}

static double RotationSum(const Outputs& out) {
	double sum = 0.0;
	for (int i = 0; i < kCount; ++i) {
		glm::quat q = out.rotations[i];
		sum += (double)(q.x * 1.0f + q.y * 2.0f + q.z * 3.0f + q.w * 4.0f);
	}
	return sum;
}

// --- timing --------------------------------------------------------------------------------------------------

static long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static void Time(const char* name, void (*kernel)(const Scene&, Outputs&, int), double (*checksum)(const Outputs&),
	const Scene& scene, Outputs& out) {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	long long start = Now();
	for (int r = 0; r < kRepeats; ++r) kernel(scene, out, r);
	double ms = (double)(Now() - start) * 1000.0 / (double)frequency.QuadPart;
	printf("%-18s %.9e %.3f\n", name, checksum(out), ms);
}

template<typename T> static T* Allocate() { return (T*)std::malloc(sizeof(T) * kCount); }

int main() {
	Scene scene = { Allocate<glm::vec3>(), Allocate<glm::vec3>(), Allocate<glm::vec3>(), Allocate<glm::quat>(), Allocate<glm::mat4>() };
	Outputs out = { Allocate<glm::mat4>(), Allocate<glm::vec3>(), Allocate<glm::quat>() };
	FillScene(scene);
	Time("build_transforms", BuildTransforms, MatrixSum, scene, out);
	Time("multiply_matrices", MultiplyMatrices, MatrixSum, scene, out);
	Time("invert_matrices", InvertMatrices, MatrixSum, scene, out);
	Time("transform_points", TransformPoints, VectorSum, scene, out);
	Time("rotate_vectors", RotateVectors, VectorSum, scene, out);
	Time("slerp_rotations", SlerpRotations, RotationSum, scene, out);
	Time("normalize_crosses", NormalizeCrosses, VectorSum, scene, out);
	return 0;
}
