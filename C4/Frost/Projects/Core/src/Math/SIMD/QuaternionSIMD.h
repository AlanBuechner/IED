#pragma once
#include "Config.h"
#include "Vector.h"
#include "Quaternion.h"
#include "SIMD/Intrinsics.h"

namespace Math::SIMD
{
#if defined(MATH_SIMD_SSE41) || defined(MATH_SIMD_AVX2)
	// ─── Quaternion multiply (q1 * q2) using SIMD ──────────────────────
	inline void MulQuat(const float* q1, const float* q2, float* out)
	{
		__m128 v1 = _mm_loadu_ps(q1);  // x, y, z, w
		__m128 v2 = _mm_loadu_ps(q2);

		// Split into xyz and w components
		__m128 xyz1 = v1;
		__m128 xyz2 = v2;
		float w1 = q1[3];
		float w2 = q2[3];

		__m128 w1Vec = _mm_set1_ps(w1);
		__m128 w2Vec = _mm_set1_ps(w2);

		// Cross product of xyz parts: xyz1 × xyz2
		__m128 zyx1 = _mm_shuffle_ps(xyz1, xyz1, _MM_SHUFFLE(1, 0, 3, 2));
		__m128 yzx1 = _mm_shuffle_ps(xyz1, xyz1, _MM_SHUFFLE(0, 3, 2, 1));
		__m128 zyx2 = _mm_shuffle_ps(xyz2, xyz2, _MM_SHUFFLE(1, 0, 3, 2));
		__m128 yzx2 = _mm_shuffle_ps(xyz2, xyz2, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 cross = _mm_sub_ps(_mm_mul_ps(xyz1, zyx2), _mm_mul_ps(yzx1, xyz2));

		// Result: w = w1*w2 - dot(xyz1, xyz2), xyz = w1*xyz2 + w2*xyz1 + cross
		__m128 dotXyz = _mm_dp_ps(xyz1, xyz2, 0x70);
		float dotVal = dotXyz.m128_f32[0];

		out[0] = w1 * q2[0] + w2 * q1[0] + cross.m128_f32[0];
		out[1] = w1 * q2[1] + w2 * q1[1] + cross.m128_f32[1];
		out[2] = w1 * q2[2] + w2 * q1[2] + cross.m128_f32[2];
		out[3] = w1 * w2 - dotVal;
	}

	// ─── Quaternion normalize using SIMD ───────────────────────────────
	inline void NormalizeQuat(const float* q, float* out)
	{
		__m128 v = _mm_loadu_ps(q);
		__m128 dot = _mm_dp_ps(v, v, 0xFF);
		float len = std::sqrt(dot.m128_f32[0]);
		if (len < 1e-6f)
		{
			out[0] = 0.f; out[1] = 0.f; out[2] = 0.f; out[3] = 1.f;
			return;
		}
		float invLen = 1.0f / len;
		__m128 scale = _mm_set1_ps(invLen);
		_mm_storeu_ps(out, _mm_mul_ps(v, scale));
	}

	// ─── Batch quaternion-vector rotate (q * v * q^-1) ────────────────
	inline void RotateVec3Batch(const float* q, const float* in, float* out, int count)
	{
		__m128 qv = _mm_loadu_ps(q);
		float qw = q[3];
		__m128 xyz = _mm_set_ps(0.f, q[2], q[1], q[0]);

		for (int i = 0; i < count; ++i)
		{
			const float* v = &in[i * 3];
			__m128 vec = _mm_set_ps(0.f, v[2], v[1], v[0]);

			// t = 2 * cross(xyz, vec)
			__m128 zyx_v = _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(1, 0, 3, 2));
			__m128 yzx_v = _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(0, 3, 2, 1));
			__m128 cross1 = _mm_sub_ps(_mm_mul_ps(xyz, zyx_v), _mm_mul_ps(yzx_v, xyz));
			__m128 two = _mm_set1_ps(2.f);
			__m128 t = _mm_mul_ps(two, cross1);

			// out = vec + qw*t + cross(xyz, t)
			__m128 qw_t = _mm_mul_ps(_mm_set1_ps(qw), t);

			__m128 zyx_t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 3, 2));
			__m128 yzx_t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 3, 2, 1));
			__m128 cross2 = _mm_sub_ps(_mm_mul_ps(xyz, zyx_t), _mm_mul_ps(yzx_t, xyz));

			__m128 result = _mm_add_ps(_mm_add_ps(vec, qw_t), cross2);
			out[i * 3 + 0] = result.m128_f32[0];
			out[i * 3 + 1] = result.m128_f32[1];
			out[i * 3 + 2] = result.m128_f32[2];
		}
	}

	// ─── Quaternion conjugate ─────────────────────────────────────────
	inline void ConjugateQuat(const float* q, float* out)
	{
		__m128 v = _mm_loadu_ps(q);
		__m128 sign = _mm_set_ps(0.f, -1.f, -1.f, -1.f);
		_mm_storeu_ps(out, _mm_mul_ps(v, sign));
	}

#else // Scalar fallback

	inline void MulQuat(const float* q1, const float* q2, float* out)
	{
		out[0] = q1[3]*q2[0] + q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1];
		out[1] = q1[3]*q2[1] - q1[0]*q2[2] + q1[1]*q2[3] + q1[2]*q2[0];
		out[2] = q1[3]*q2[2] + q1[0]*q2[1] - q1[1]*q2[0] + q1[2]*q2[3];
		out[3] = q1[3]*q2[3] - q1[0]*q2[0] - q1[1]*q2[1] - q1[2]*q2[2];
	}

	inline void NormalizeQuat(const float* q, float* out)
	{
		float len = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
		if (len < 1e-6f) { out[0]=0; out[1]=0; out[2]=0; out[3]=1; return; }
		float inv = 1.0f / len;
		out[0] = q[0]*inv; out[1] = q[1]*inv; out[2] = q[2]*inv; out[3] = q[3]*inv;
	}

	inline void RotateVec3Batch(const float* q, const float* in, float* out, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			Vector3<float> v(in[i*3], in[i*3+1], in[i*3+2]);
			Quaternion qq(q[0], q[1], q[2], q[3]);
			Vector3<float> r = qq.RotateVector(v);
			out[i*3] = r.x; out[i*3+1] = r.y; out[i*3+2] = r.z;
		}
	}

	inline void ConjugateQuat(const float* q, float* out)
	{
		out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
	}

#endif
}
