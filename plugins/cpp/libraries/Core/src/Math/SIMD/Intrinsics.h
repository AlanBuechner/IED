#pragma once
#include "Config.h"

#if defined(MATH_SIMD_SSE41) || defined(MATH_SIMD_AVX2)
	#include <immintrin.h>
#endif

namespace Math::SIMD
{
#if defined(MATH_SIMD_SSE41) || defined(MATH_SIMD_AVX2)
	// ─── Load / Store ──────────────────────────────────────────────────────
	inline __m128 Load(const float* p) { return _mm_loadu_ps(p); }
	inline __m128 LoadAligned(const float* p) { return _mm_load_ps(p); }
	inline void Store(float* p, __m128 v) { _mm_storeu_ps(p, v); }
	inline void StoreAligned(float* p, __m128 v) { _mm_store_ps(p, v); }

	// ─── Arithmetic ────────────────────────────────────────────────────────
	inline __m128 Add(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
	inline __m128 Sub(__m128 a, __m128 b) { return _mm_sub_ps(a, b); }
	inline __m128 Mul(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
	inline __m128 Div(__m128 a, __m128 b) { return _mm_div_ps(a, b); }

#if defined(MATH_SIMD_AVX2)
	// FMA: a*b + c (single instruction, better precision than separate mul+add)
	inline __m128 FMA(__m128 a, __m128 b, __m128 c) { return _mm_fmadd_ps(a, b, c); }
#endif

	// ─── Comparison (returns mask with 0xFFFFFFFF or 0x0 per lane) ─────────
	inline __m128 Greater(__m128 a, __m128 b) { return _mm_cmpgt_ps(a, b); }
	inline __m128 Less(__m128 a, __m128 b) { return _mm_cmplt_ps(a, b); }
	inline __m128 Equal(__m128 a, __m128 b) { return _mm_cmpeq_ps(a, b); }

	// ─── Blend / Select (SSE4.1+) ──────────────────────────────────────────
	inline __m128 Blend(__m128 a, __m128 b, __m128 mask) { return _mm_blendv_ps(a, b, mask); }

	// ─── Shuffle (SSE3+ for horizontal operations, SSE4.1 for general) ─────
	inline __m128 Shuffle(__m128 a, const int imm8) { return _mm_shuffle_ps(a, a, imm8); }

	// ─── Approximate reciprocal / inverse square root ──────────────────────
	inline __m128 RCP(__m128 a) { return _mm_rcp_ps(a); }
	inline __m128 RSQRT(__m128 a) { return _mm_rsqrt_ps(a); }

	// ─── Horizontal dot product ────────────────────────────────────────────
	inline float HDot(__m128 a, __m128 b)
	{
#if defined(MATH_SIMD_AVX2)
		__m128 prod = _mm_mul_ps(a, b);
		__m128 hi = _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(2, 3, 0, 1)); // swap pairs
		__m128 sum = _mm_add_ps(prod, hi);
		__m128 final = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0, 1, 2, 3)); // duplicate lane 0+1
		return sum.m128_f32[0] + final.m128_f32[0];
#else
		__m128 prod = _mm_mul_ps(a, b);
		__m128 shuf = _mm_shuffle_ps(prod, prod, _MM_SHUFFLE(1, 0, 3, 2));
		__m128 sums = _mm_add_ps(prod, shuf);
		return _mm_cvtss_f32(_mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0, 0, 0, 0)));
#endif
	}

	// ─── Constants (compile-time constructed) ──────────────────────────────
	inline constexpr __m128 Zero() { return _mm_setzero_ps(); }
	inline constexpr __m128 One() { return _mm_set_ps(1.0f, 1.0f, 1.0f, 1.0f); }

#else // Scalar fallback — still works, just no SIMD

	struct ScalarSimd
	{
		float x, y, z, w;
	};

	inline ScalarSimd Load(const float* p) { return { p[0], p[1], p[2], p[3] }; }
	inline ScalarSimd LoadAligned(const float* p) { return Load(p); }
	inline void Store(float* p, ScalarSimd v) { p[0] = v.x; p[1] = v.y; p[2] = v.z; p[3] = v.w; }
	inline void StoreAligned(float* p, ScalarSimd v) { Store(p, v); }

	inline ScalarSimd Add(ScalarSimd a, ScalarSimd b) { return { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
	inline ScalarSimd Sub(ScalarSimd a, ScalarSimd b) { return { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
	inline ScalarSimd Mul(ScalarSimd a, ScalarSimd b) { return { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; }
	inline ScalarSimd Div(ScalarSimd a, ScalarSimd b) { return { a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w }; }

	inline ScalarSimd FMA(ScalarSimd a, ScalarSimd b, ScalarSimd c)
	{
		return { a.x * b.x + c.x, a.y * b.y + c.y, a.z * b.z + c.z, a.w * b.w + c.w };
	}

	inline ScalarSimd Greater(ScalarSimd a, ScalarSimd b)
	{
		return { a.x > b.x ? 0xFFFFFFFFf : 0.0f, a.y > b.y ? 0xFFFFFFFFf : 0.0f,
				 a.z > b.z ? 0xFFFFFFFFf : 0.0f, a.w > b.w ? 0xFFFFFFFFf : 0.0f };
	}

	inline ScalarSimd Less(ScalarSimd a, ScalarSimd b)
	{
		return { a.x < b.x ? 0xFFFFFFFFf : 0.0f, a.y < b.y ? 0xFFFFFFFFf : 0.0f,
				 a.z < b.z ? 0xFFFFFFFFf : 0.0f, a.w < b.w ? 0xFFFFFFFFf : 0.0f };
	}

	inline ScalarSimd Equal(ScalarSimd a, ScalarSimd b)
	{
		return { a.x == b.x ? 0xFFFFFFFFf : 0.0f, a.y == b.y ? 0xFFFFFFFFf : 0.0f,
				 a.z == b.z ? 0xFFFFFFFFf : 0.0f, a.w == b.w ? 0xFFFFFFFFf : 0.0f };
	}

	inline ScalarSimd Blend(ScalarSimd a, ScalarSimd b, ScalarSimd mask)
	{
		return { mask.x ? b.x : a.x, mask.y ? b.y : a.y, mask.z ? b.z : a.z, mask.w ? b.w : a.w };
	}

	inline float HDot(ScalarSimd a, ScalarSimd b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	}

	inline constexpr ScalarSimd Zero() { return { 0.0f, 0.0f, 0.0f, 0.0f }; }
	inline constexpr ScalarSimd One() { return { 1.0f, 1.0f, 1.0f, 1.0f }; }

#endif
}
