// Batch math benchmark - C++ side of batch.cf, built with -march=x86-64-v3 (AVX2, FMA). Built twice by
// run-benchmark.ps1: with BATCH_AVX2, hand-written AVX2 intrinsics over structure-of-arrays batches of eight vectors
// (the data stays in that form, as batch.cf keeps it); without, the per-vector GLM loops a GLM program would write
// over arrays of vectors (which the compiler may vectorize). The checksums are summed in the same vector order.
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "../glm/glm/glm.hpp"
#include "../glm/glm/gtc/quaternion.hpp"
#include <immintrin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define NOMINMAX
#include <windows.h>

static const int kCount = 4096;
static const int kLanes = 8;
static const int kBatches = kCount / kLanes;
static const int kBatchMask = kBatches - 1;
static const int kRepeats = 400;
static const float kDt = 1.0f / 60.0f;

static float Wave(int i, int lane) { return std::sin((float)(i * 7 + lane * 13) * 0.01f); }
static glm::vec3 WaveVector(int i, int lane) { return glm::vec3(Wave(i, lane), Wave(i, lane + 1), Wave(i, lane + 2)); }

// What every kernel reads: batch.cf makes the same values.
struct Scene {
	glm::mat4 matrices[64];
	glm::quat rotations[kBatches];
	glm::vec3 eyes[kBatches];
	glm::vec3 positions[kCount];
	glm::vec3 velocities[kCount];
	glm::vec3 directions[kCount];
};

static void FillScene(Scene& scene) {
	for (int m = 0; m < 64; ++m) {
		glm::vec3 a = WaveVector(m, 20);
		scene.matrices[m] = glm::mat4(glm::vec4(a.x + 1.0f, a.y, a.z, 0.0f), glm::vec4(a.y, a.z + 1.0f, a.x, 0.0f),
			glm::vec4(a.z, a.x, a.y + 1.0f, 0.0f), glm::vec4(a * 5.0f, 1.0f));
	}
	for (int b = 0; b < kBatches; ++b) {
		scene.rotations[b] = glm::angleAxis(Wave(b, 9) * 3.14159265f, glm::normalize(WaveVector(b, 10) + glm::vec3(0.0f, 2.0f, 0.0f)));
		scene.eyes[b] = WaveVector(b, 30) * 4.0f;
	}
	for (int i = 0; i < kCount; ++i) {
		scene.positions[i] = WaveVector(i, 0) * 10.0f;
		scene.velocities[i] = WaveVector(i, 3) * 5.0f;
		scene.directions[i] = WaveVector(i, 6) + glm::vec3(0.0f, 0.0f, 1.5f);
	}
}

static double VectorChecksum(float x, float y, float z) { return (double)(x * 1.0f + y * 2.0f + z * 3.0f); }

static long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static double Milliseconds(long long ticks) {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	return (double)ticks * 1000.0 / (double)frequency.QuadPart;
}

#ifdef BATCH_AVX2
// --- hand-written AVX2: eight vectors per register, one register per component -------------------------------
struct V3x8 { __m256 x, y, z; };

// The pointers batch.cf's SceneView holds. A __m256 store may alias anything to clang (a vector type is char to its
// alias analysis), so this loop reloads them after every store.
struct BatchView {
	const glm::mat4* matrices;
	const V3x8* positions;
	V3x8* out;
};

struct Batches {
	V3x8 positions[kBatches];
	V3x8 velocities[kBatches];
	V3x8 directions[kBatches];
	V3x8 out[kBatches];
	BatchView view;
};

static V3x8 Gather(const glm::vec3* v) {
	const __m256i stride = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);
	const float* f = &v[0].x;
	return { _mm256_i32gather_ps(f, stride, 4), _mm256_i32gather_ps(f + 1, stride, 4), _mm256_i32gather_ps(f + 2, stride, 4) };
}

static V3x8 TransformPoint(const glm::mat4& m, const V3x8& p) {
	return {
		_mm256_fmadd_ps(p.z, _mm256_set1_ps(m[2][0]), _mm256_fmadd_ps(p.y, _mm256_set1_ps(m[1][0]), _mm256_fmadd_ps(p.x, _mm256_set1_ps(m[0][0]), _mm256_set1_ps(m[3][0])))),
		_mm256_fmadd_ps(p.z, _mm256_set1_ps(m[2][1]), _mm256_fmadd_ps(p.y, _mm256_set1_ps(m[1][1]), _mm256_fmadd_ps(p.x, _mm256_set1_ps(m[0][1]), _mm256_set1_ps(m[3][1])))),
		_mm256_fmadd_ps(p.z, _mm256_set1_ps(m[2][2]), _mm256_fmadd_ps(p.y, _mm256_set1_ps(m[1][2]), _mm256_fmadd_ps(p.x, _mm256_set1_ps(m[0][2]), _mm256_set1_ps(m[3][2])))) };
}

static V3x8 Cross(const V3x8& a, const V3x8& b) {
	return { _mm256_fmsub_ps(a.y, b.z, _mm256_mul_ps(a.z, b.y)), _mm256_fmsub_ps(a.z, b.x, _mm256_mul_ps(a.x, b.z)),
		_mm256_fmsub_ps(a.x, b.y, _mm256_mul_ps(a.y, b.x)) };
}

static V3x8 Rotate(const glm::quat& q, const V3x8& v) {
	const V3x8 u = { _mm256_set1_ps(q.x), _mm256_set1_ps(q.y), _mm256_set1_ps(q.z) };
	const V3x8 uv = Cross(u, v);
	const V3x8 uuv = Cross(u, uv);
	const __m256 w = _mm256_set1_ps(q.w);
	const __m256 two = _mm256_set1_ps(2.0f);
	return { _mm256_fmadd_ps(_mm256_fmadd_ps(uv.x, w, uuv.x), two, v.x), _mm256_fmadd_ps(_mm256_fmadd_ps(uv.y, w, uuv.y), two, v.y),
		_mm256_fmadd_ps(_mm256_fmadd_ps(uv.z, w, uuv.z), two, v.z) };
}

static __m256 Length(const V3x8& v) { return _mm256_sqrt_ps(_mm256_fmadd_ps(v.z, v.z, _mm256_fmadd_ps(v.y, v.y, _mm256_mul_ps(v.x, v.x)))); }

static V3x8 Normalized(const V3x8& v) {
	const __m256 length = Length(v);
	const __m256 positive = _mm256_cmp_ps(length, _mm256_setzero_ps(), _CMP_GT_OQ);
	const __m256 scale = _mm256_blendv_ps(_mm256_set1_ps(1.0f), _mm256_div_ps(_mm256_set1_ps(1.0f), length), positive);
	return { _mm256_mul_ps(v.x, scale), _mm256_mul_ps(v.y, scale), _mm256_mul_ps(v.z, scale) };
}

static void TransformPoints(const Scene& scene, Batches& data, int shift) {
	for (int b = 0; b < kBatches; ++b) data.out[b] = TransformPoint(scene.matrices[(b + shift) & 63], data.positions[b]);
}

static void TransformThroughPointers(const Scene& scene, Batches& data, int shift) {
	BatchView& view = data.view;
	for (int b = 0; b < kBatches; ++b) view.out[b] = TransformPoint(view.matrices[(b + shift) & 63], view.positions[b]);
}

static void RotateVectors(const Scene& scene, Batches& data, int shift) {
	for (int b = 0; b < kBatches; ++b) data.out[b] = Rotate(scene.rotations[(b + shift) & kBatchMask], data.positions[b]);
}

static void NormalizeCrosses(const Scene& scene, Batches& data, int shift) {
	for (int b = 0; b < kBatches; ++b) data.out[b] = Normalized(Cross(data.positions[b], data.directions[(b + shift) & kBatchMask]));
}

static void Integrate(const Scene& scene, Batches& data, int shift) {
	const __m256 dt = _mm256_set1_ps(kDt * (float)(shift % 7 + 1));
	const __m256 high = _mm256_set1_ps(10.0f);
	const __m256 low = _mm256_set1_ps(-10.0f);
	for (int b = 0; b < kBatches; ++b) {
		V3x8 p = data.positions[b];
		V3x8 v = data.velocities[b];
		v.y = _mm256_fnmadd_ps(_mm256_set1_ps(9.8f), dt, v.y);
		p.x = _mm256_fmadd_ps(v.x, dt, p.x);
		p.y = _mm256_fmadd_ps(v.y, dt, p.y);
		p.z = _mm256_fmadd_ps(v.z, dt, p.z);
		p.y = _mm256_blendv_ps(p.y, _mm256_sub_ps(_mm256_setzero_ps(), p.y), _mm256_cmp_ps(p.y, _mm256_setzero_ps(), _CMP_LT_OQ));
		p.x = _mm256_min_ps(_mm256_max_ps(p.x, low), high);
		p.z = _mm256_min_ps(_mm256_max_ps(p.z, low), high);
		data.out[b] = p;
	}
}

static void Distances(const Scene& scene, Batches& data, int shift) {
	for (int b = 0; b < kBatches; ++b) {
		const glm::vec3 eye = scene.eyes[(b + shift) & kBatchMask];
		const V3x8& p = data.positions[b];
		const V3x8 d = { _mm256_sub_ps(p.x, _mm256_set1_ps(eye.x)), _mm256_sub_ps(p.y, _mm256_set1_ps(eye.y)), _mm256_sub_ps(p.z, _mm256_set1_ps(eye.z)) };
		data.out[b] = { Length(d), _mm256_setzero_ps(), _mm256_setzero_ps() };
	}
}

static double Checksum(const Batches& data) {
	double sum = 0.0;
	for (int b = 0; b < kBatches; ++b) {
		alignas(32) float x[8], y[8], z[8];
		_mm256_store_ps(x, data.out[b].x); _mm256_store_ps(y, data.out[b].y); _mm256_store_ps(z, data.out[b].z);
		for (int i = 0; i < kLanes; ++i) sum += VectorChecksum(x[i], y[i], z[i]);
	}
	return sum;
}

static void Time(const char* name, void (*kernel)(const Scene&, Batches&, int), const Scene& scene, Batches& data) {
	long long start = Now();
	for (int r = 0; r < kRepeats; ++r) kernel(scene, data, r);
	printf("%-18s %.9e %.3f\n", name, Checksum(data), Milliseconds(Now() - start));
}

int main() {
	static Scene scene;
	static Batches data;
	FillScene(scene);
	for (int b = 0; b < kBatches; ++b) {
		data.positions[b] = Gather(&scene.positions[b * kLanes]);
		data.velocities[b] = Gather(&scene.velocities[b * kLanes]);
		data.directions[b] = Gather(&scene.directions[b * kLanes]);
	}
	data.view = { &scene.matrices[0], &data.positions[0], &data.out[0] };
	Time("batch_transform", TransformPoints, scene, data);
	Time("batch_pointers", TransformThroughPointers, scene, data);
	Time("batch_rotate", RotateVectors, scene, data);
	Time("batch_normalize", NormalizeCrosses, scene, data);
	Time("batch_integrate", Integrate, scene, data);
	Time("batch_distance", Distances, scene, data);
	return 0;
}
#else
// --- plain GLM, one vector at a time ---------------------------------------------------------------------------
static glm::vec3 g_out[kCount];

struct VectorView {
	const glm::mat4* matrices;
	const glm::vec3* positions;
	glm::vec3* out;
};

static VectorView g_view;

static void TransformPoints(const Scene& scene, int shift) {
	for (int i = 0; i < kCount; ++i) g_out[i] = glm::vec3(scene.matrices[(i / kLanes + shift) & 63] * glm::vec4(scene.positions[i], 1.0f));
}

static void TransformThroughPointers(const Scene& scene, int shift) {
	VectorView& view = g_view;
	for (int i = 0; i < kCount; ++i) view.out[i] = glm::vec3(view.matrices[(i / kLanes + shift) & 63] * glm::vec4(view.positions[i], 1.0f));
}

static void RotateVectors(const Scene& scene, int shift) {
	for (int i = 0; i < kCount; ++i) g_out[i] = scene.rotations[(i / kLanes + shift) & kBatchMask] * scene.positions[i];
}

static void NormalizeCrosses(const Scene& scene, int shift) {
	for (int i = 0; i < kCount; ++i)
		g_out[i] = glm::normalize(glm::cross(scene.positions[i], scene.directions[((i / kLanes + shift) & kBatchMask) * kLanes + i % kLanes]));
}

static void Integrate(const Scene& scene, int shift) {
	const float dt = kDt * (float)(shift % 7 + 1);
	for (int i = 0; i < kCount; ++i) {
		glm::vec3 v = scene.velocities[i];
		v.y -= 9.8f * dt;
		glm::vec3 p = scene.positions[i] + v * dt;
		if (p.y < 0.0f) p.y = -p.y;
		p.x = glm::min(glm::max(p.x, -10.0f), 10.0f);
		p.z = glm::min(glm::max(p.z, -10.0f), 10.0f);
		g_out[i] = p;
	}
}

static void Distances(const Scene& scene, int shift) {
	for (int i = 0; i < kCount; ++i) g_out[i] = glm::vec3(glm::length(scene.positions[i] - scene.eyes[(i / kLanes + shift) & kBatchMask]), 0.0f, 0.0f);
}

static double Checksum() {
	double sum = 0.0;
	for (int i = 0; i < kCount; ++i) sum += VectorChecksum(g_out[i].x, g_out[i].y, g_out[i].z);
	return sum;
}

static void Time(const char* name, void (*kernel)(const Scene&, int), const Scene& scene) {
	long long start = Now();
	for (int r = 0; r < kRepeats; ++r) kernel(scene, r);
	printf("%-18s %.9e %.3f\n", name, Checksum(), Milliseconds(Now() - start));
}

int main() {
	static Scene scene;
	FillScene(scene);
	g_view = { &scene.matrices[0], &scene.positions[0], &g_out[0] };
	Time("batch_transform", TransformPoints, scene);
	Time("batch_pointers", TransformThroughPointers, scene);
	Time("batch_rotate", RotateVectors, scene);
	Time("batch_normalize", NormalizeCrosses, scene);
	Time("batch_integrate", Integrate, scene);
	Time("batch_distance", Distances, scene);
	return 0;
}
#endif
