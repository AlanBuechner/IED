#pragma once
#include "Config.h"
#include "Vector.h"
#include "Matrix.h"
#include "SIMD/Intrinsics.h"

namespace Math::SIMD
{
#if defined(MATH_SIMD_SSE41) || defined(MATH_SIMD_AVX2)
	// --- Matrix multiply (column-major Mat4 x Mat4 -> Mat4) --------------
	inline void MulMat4(const float* a, const float* b, float* out)
	{
		// Load columns of B into SIMD registers
		__m128 b0 = _mm_loadu_ps(&b[0]);
		__m128 b1 = _mm_loadu_ps(&b[4]);
		__m128 b2 = _mm_loadu_ps(&b[8]);
		__m128 b3 = _mm_loadu_ps(&b[12]);

		// For each column of A, compute the resulting column
		for (int col = 0; col < 4; ++col)
		{
			float a00 = a[col * 4 + 0];
			float a01 = a[col * 4 + 1];
			float a02 = a[col * 4 + 2];
			float a03 = a[col * 4 + 3];

			__m128 aRow = _mm_set_ps(a00, a01, a02, a03);

			__m128 r0 = _mm_mul_ps(b0, aRow);
			__m128 r1 = _mm_mul_ps(b1, aRow);
			__m128 r2 = _mm_mul_ps(b2, aRow);
			__m128 r3 = _mm_mul_ps(b3, aRow);

			__m128 result = _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3));
			_mm_storeu_ps(&out[col * 4], result);
		}
	}

	// --- Matrix transpose (column-major Mat4 -> column-major Mat4) -------
	inline void TransposeMat4(const float* m, float* out)
	{
		__m128 c0 = _mm_loadu_ps(&m[0]);
		__m128 c1 = _mm_loadu_ps(&m[4]);
		__m128 c2 = _mm_loadu_ps(&m[8]);
		__m128 c3 = _mm_loadu_ps(&m[12]);

		// Shuffle columns into rows
		__m128 r0 = _mm_shuffle_ps(c0, c3, _MM_SHUFFLE(2, 1, 0, 0));
		__m128 r1 = _mm_shuffle_ps(c0, c3, _MM_SHUFFLE(3, 2, 1, 0));
		__m128 r2 = _mm_shuffle_ps(c1, c2, _MM_SHUFFLE(2, 1, 0, 0));
		__m128 r3 = _mm_shuffle_ps(c1, c2, _MM_SHUFFLE(3, 2, 1, 0));

		// Unpack to get transposed columns
		_mm_storeu_ps(&out[0], _mm_unpacklo_ps(r0, r2));
		_mm_storeu_ps(&out[4], _mm_unpackhi_ps(r0, r2));
		_mm_storeu_ps(&out[8], _mm_unpacklo_ps(r1, r3));
		_mm_storeu_ps(&out[12], _mm_unpackhi_ps(r1, r3));
	}

	// --- Matrix-vector multiply (Mat4 x Vector4) ------------------------
	inline void MulMat4Vec4(const float* m, const float* v, float* out)
	{
		__m128 vec = _mm_loadu_ps(v);

		__m128 c0 = _mm_loadu_ps(&m[0]);
		__m128 c1 = _mm_loadu_ps(&m[4]);
		__m128 c2 = _mm_loadu_ps(&m[8]);
		__m128 c3 = _mm_loadu_ps(&m[12]);

		__m128 r0 = _mm_mul_ps(c0, vec);
		__m128 r1 = _mm_mul_ps(c1, vec);
		__m128 r2 = _mm_mul_ps(c2, vec);
		__m128 r3 = _mm_mul_ps(c3, vec);

		__m128 result = _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3));
		_mm_storeu_ps(out, result);
	}

	// --- Batch transform: apply same Mat4 to N vectors ------------------
	inline void TransformVec4Batch(const float* m, const float* in, float* out, int count)
	{
		__m128 c0 = _mm_loadu_ps(&m[0]);
		__m128 c1 = _mm_loadu_ps(&m[4]);
		__m128 c2 = _mm_loadu_ps(&m[8]);
		__m128 c3 = _mm_loadu_ps(&m[12]);

		for (int i = 0; i < count; ++i)
		{
			const float* v = &in[i * 4];
			__m128 vec = _mm_loadu_ps(v);

			__m128 r0 = _mm_mul_ps(c0, vec);
			__m128 r1 = _mm_mul_ps(c1, vec);
			__m128 r2 = _mm_mul_ps(c2, vec);
			__m128 r3 = _mm_mul_ps(c3, vec);

			__m128 result = _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3));
			_mm_storeu_ps(&out[i * 4], result);
		}
	}

	// --- Batch transform positions: Mat4 x Vector3 (w=1) ----------------
	inline void TransformVec3Batch(const float* m, const float* in, float* out, int count)
	{
		__m128 c0 = _mm_loadu_ps(&m[0]);
		__m128 c1 = _mm_loadu_ps(&m[4]);
		__m128 c2 = _mm_loadu_ps(&m[8]);
		__m128 t  = _mm_loadu_ps(&m[12]);

		for (int i = 0; i < count; ++i)
		{
			const float* v = &in[i * 3];
			__m128 vec = _mm_set_ps(1.f, v[2], v[1], v[0]);

			__m128 r0 = _mm_mul_ps(c0, vec);
			__m128 r1 = _mm_mul_ps(c1, vec);
			__m128 r2 = _mm_mul_ps(c2, vec);

			__m128 result = _mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, t));
			float temp[4];
			_mm_storeu_ps(temp, result);
			out[i * 3 + 0] = temp[0];
			out[i * 3 + 1] = temp[1];
			out[i * 3 + 2] = temp[2];
		}
	}

	// --- Component-wise matrix multiply (Hadamard product) --------------
	inline void Mat4CompMult(const float* a, const float* b, float* out)
	{
		for (int i = 0; i < 4; ++i)
		{
			__m128 va = _mm_loadu_ps(&a[i * 4]);
			__m128 vb = _mm_loadu_ps(&b[i * 4]);
			_mm_storeu_ps(&out[i * 4], _mm_mul_ps(va, vb));
		}
	}

#else // Scalar fallback

	inline void MulMat4(const float* a, const float* b, float* out)
	{
		for (int col = 0; col < 4; ++col)
		{
			for (int row = 0; row < 4; ++row)
			{
				float sum = 0.f;
				for (int k = 0; k < 4; ++k)
					sum += a[k * 4 + col] * b[col * 4 + row];
				out[col * 4 + row] = sum;
			}
		}
	}

	inline void TransposeMat4(const float* m, float* out)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				out[c * 4 + r] = m[r * 4 + c];
	}

	inline void MulMat4Vec4(const float* m, const float* v, float* out)
	{
		for (int row = 0; row < 4; ++row)
		{
			out[row] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
			m++;
		}
	}

	inline void TransformVec4Batch(const float* m, const float* in, float* out, int count)
	{
		for (int i = 0; i < count; ++i)
			MulMat4Vec4(m, &in[i * 4], &out[i * 4]);
	}

	inline void TransformVec3Batch(const float* m, const float* in, float* out, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			const float* v = &in[i * 3];
			float* o = &out[i * 3];
			o[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
			o[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
			o[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
		}
	}

	inline void Mat4CompMult(const float* a, const float* b, float* out)
	{
		for (int i = 0; i < 16; ++i)
			out[i] = a[i] * b[i];
	}

#endif
}
