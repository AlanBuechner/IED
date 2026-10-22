#pragma once
#include "Config.h"
#include "Vector.h"
#include "SIMD/Intrinsics.h"

namespace Math::SIMD
{
#if defined(MATH_SIMD_SSE41) || defined(MATH_SIMD_AVX2)
	// --- Vector4 operations via SIMD ------------------------------------

	inline __m128 Vec4Load(const Vector4<float>& v) { return _mm_loadu_ps(&v.x); }
	inline void Vec4Store(Vector4<float>& out, __m128 v) { _mm_storeu_ps(&out.x, v); }

	inline __m128 Vec4Add(__m128 a, __m128 b) { return _mm_add_ps(a, b); }
	inline __m128 Vec4Sub(__m128 a, __m128 b) { return _mm_sub_ps(a, b); }
	inline __m128 Vec4Mul(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
	inline __m128 Vec4Div(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
	inline __m128 Vec4Neg(__m128 a)
	{
		__m128 sign = _mm_set1_ps(-1.f);
		return _mm_mul_ps(a, sign);
	}

	// Dot product of two Vector4s
	inline float Vec4Dot(const Vector4<float>& a, const Vector4<float>& b)
	{
		__m128 va = _mm_loadu_ps(&a.x);
		__m128 vb = _mm_loadu_ps(&b.x);
		return HDot(va, vb);
	}

	// Length of a Vector4
	inline float Vec4Length(const Vector4<float>& v)
	{
		__m128 va = _mm_loadu_ps(&v.x);
		return std::sqrt(HDot(va, va));
	}

	// Normalize a Vector4
	inline void Vec4Normalize(const Vector4<float>& in, Vector4<float>& out)
	{
		__m128 v = _mm_loadu_ps(&in.x);
		float len = std::sqrt(HDot(v, v));
		if (len < 1e-6f) { out = Vector4<float>::Zero(); return; }
		float invLen = 1.0f / len;
		_mm_storeu_ps(&out.x, _mm_mul_ps(v, _mm_set1_ps(invLen)));
	}

	// Lerp between two Vector4s
	inline void Vec4Lerp(const Vector4<float>& a, const Vector4<float>& b, float t, Vector4<float>& out)
	{
		__m128 va = _mm_loadu_ps(&a.x);
		__m128 vb = _mm_loadu_ps(&b.x);
		__m128 diff = _mm_sub_ps(vb, va);
		__m128 tVec = _mm_set1_ps(t);
		_mm_storeu_ps(&out.x, _mm_add_ps(va, _mm_mul_ps(diff, tVec)));
	}

	// Component-wise min/max
	inline void Vec4Min(const Vector4<float>& a, const Vector4<float>& b, Vector4<float>& out)
	{
		__m128 va = _mm_loadu_ps(&a.x);
		__m128 vb = _mm_loadu_ps(&b.x);
		_mm_storeu_ps(&out.x, _mm_min_ps(va, vb));
	}

	inline void Vec4Max(const Vector4<float>& a, const Vector4<float>& b, Vector4<float>& out)
	{
		__m128 va = _mm_loadu_ps(&a.x);
		__m128 vb = _mm_loadu_ps(&b.x);
		_mm_storeu_ps(&out.x, _mm_max_ps(va, vb));
	}

	// --- Vector3 operations via SIMD (stored as x,y,z,0) --------------

	inline __m128 Vec3Load(const Vector3<float>& v) { return _mm_set_ps(0.f, v.z, v.y, v.x); }
	inline void Vec3Store(Vector3<float>& out, __m128 v)
	{
		float temp[4];
		_mm_storeu_ps(temp, v);
		out.x = temp[0]; out.y = temp[1]; out.z = temp[2];
	}

	// Dot product of two Vector3s
	inline float Vec3Dot(const Vector3<float>& a, const Vector3<float>& b)
	{
		__m128 va = _mm_set_ps(0.f, a.z, a.y, a.x);
		__m128 vb = _mm_set_ps(0.f, b.z, b.y, b.x);
		return HDot(va, vb);
	}

	// Length of a Vector3
	inline float Vec3Length(const Vector3<float>& v)
	{
		__m128 va = _mm_set_ps(0.f, v.z, v.y, v.x);
		return std::sqrt(HDot(va, va));
	}

	// Normalize a Vector3
	inline void Vec3Normalize(const Vector3<float>& in, Vector3<float>& out)
	{
		__m128 v = _mm_set_ps(0.f, in.z, in.y, in.x);
		float len = std::sqrt(HDot(v, v));
		if (len < 1e-6f) { out = Vector3<float>::Zero(); return; }
		float invLen = 1.0f / len;
		__m128 result = _mm_mul_ps(v, _mm_set1_ps(invLen));
		float temp[4];
		_mm_storeu_ps(temp, result);
		out.x = temp[0]; out.y = temp[1]; out.z = temp[2];
	}

	// Cross product of two Vector3s
	inline void Vec3Cross(const Vector3<float>& a, const Vector3<float>& b, Vector3<float>& out)
	{
		__m128 va = _mm_set_ps(0.f, a.z, a.y, a.x);
		__m128 vb = _mm_set_ps(0.f, b.z, b.y, b.x);

		__m128 zyx_a = _mm_shuffle_ps(va, va, _MM_SHUFFLE(1, 0, 3, 2));
		__m128 yzx_a = _mm_shuffle_ps(va, va, _MM_SHUFFLE(0, 3, 2, 1));
		__m128 zyx_b = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(1, 0, 3, 2));
		__m128 yzx_b = _mm_shuffle_ps(vb, vb, _MM_SHUFFLE(0, 3, 2, 1));

		__m128 cross = _mm_sub_ps(_mm_mul_ps(va, zyx_b), _mm_mul_ps(yzx_a, vb));
		float temp[4];
		_mm_storeu_ps(temp, cross);
		out.x = temp[0]; out.y = temp[1]; out.z = temp[2];
	}

	// Lerp between two Vector3s
	inline void Vec3Lerp(const Vector3<float>& a, const Vector3<float>& b, float t, Vector3<float>& out)
	{
		__m128 va = _mm_set_ps(0.f, a.z, a.y, a.x);
		__m128 vb = _mm_set_ps(0.f, b.z, b.y, b.x);
		__m128 diff = _mm_sub_ps(vb, va);
		__m128 tVec = _mm_set1_ps(t);
		__m128 result = _mm_add_ps(va, _mm_mul_ps(diff, tVec));
		float temp[4];
		_mm_storeu_ps(temp, result);
		out.x = temp[0]; out.y = temp[1]; out.z = temp[2];
	}

	// Batch normalize N Vector3s
	inline void Vec3NormalizeBatch(const float* in, float* out, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			const float* v = &in[i * 3];
			__m128 va = _mm_set_ps(0.f, v[2], v[1], v[0]);
			float len = std::sqrt(HDot(va, va));
			if (len < 1e-6f)
			{
				out[i * 3] = 0.f; out[i * 3 + 1] = 0.f; out[i * 3 + 2] = 0.f;
			}
			else
			{
				float invLen = 1.0f / len;
				__m128 result = _mm_mul_ps(va, _mm_set1_ps(invLen));
				float temp[4];
				_mm_storeu_ps(temp, result);
				out[i * 3] = temp[0]; out[i * 3 + 1] = temp[1]; out[i * 3 + 2] = temp[2];
			}
		}
	}

#else // Scalar fallback

	inline void Vec4Normalize(const Vector4<float>& in, Vector4<float>& out) { out = Normalized(in); }
	inline float Vec4Dot(const Vector4<float>& a, const Vector4<float>& b) { return Dot(a, b); }
	inline float Vec4Length(const Vector4<float>& v) { return Length(v); }
	inline void Vec4Lerp(const Vector4<float>& a, const Vector4<float>& b, float t, Vector4<float>& out) { out = Lerp(a, b, t); }
	inline void Vec4Min(const Vector4<float>& a, const Vector4<float>& b, Vector4<float>& out) { out = Min(a, b); }
	inline void Vec4Max(const Vector4<float>& a, const Vector4<float>& b, Vector4<float>& out) { out = Max(a, b); }

	inline float Vec3Dot(const Vector3<float>& a, const Vector3<float>& b) { return Dot(a, b); }
	inline float Vec3Length(const Vector3<float>& v) { return Length(v); }
	inline void Vec3Normalize(const Vector3<float>& in, Vector3<float>& out) { out = Normalized(in); }
	inline void Vec3Cross(const Vector3<float>& a, const Vector3<float>& b, Vector3<float>& out) { out = Cross(a, b); }
	inline void Vec3Lerp(const Vector3<float>& a, const Vector3<float>& b, float t, Vector3<float>& out) { out = Lerp(a, b, t); }

	inline void Vec3NormalizeBatch(const float* in, float* out, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			Vector3<float> v(in[i*3], in[i*3+1], in[i*3+2]);
			Vector3<float> n = Normalized(v);
			out[i*3] = n.x; out[i*3+1] = n.y; out[i*3+2] = n.z;
		}
	}

#endif
}
