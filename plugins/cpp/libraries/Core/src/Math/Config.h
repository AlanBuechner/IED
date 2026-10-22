#pragma once

// --- Coordinate system defines (DX12 conventions) --------------------------
#define MATH_LEFT_HANDED
#define MATH_NDC_ZERO_TO_ONE

// --- SIMD feature detection via compiler builtins + preprocessor ------------
#if defined(__AVX2__) && defined(__AVX__)
	#define MATH_SIMD_AVX2 1
#elif defined(__SSE4_1__) && defined(__SSE__)
	#define MATH_SIMD_SSE41 1
#else
	#define MATH_SIMD_SCALAR 1
#endif

// Alignment helper for SIMD-friendly memory layout
#if defined(MATH_SIMD_AVX2)
	#define MATH_ALIGN 32
#elif defined(MATH_SIMD_SSE41)
	#define MATH_ALIGN 16
#else
	#define MATH_ALIGN 8
#endif

// --- Math constants --------------------------------------------------------
namespace Math
{
	constexpr float PI = static_cast<float>(3.14159265358979323846);
	constexpr float TAU = PI * 2.0f;
	constexpr float E = static_cast<float>(2.71828182845904523536);
	constexpr float EPSILON = 1e-10f;
	constexpr float DEG_TO_RAD = PI / 180.0f;
	constexpr float RAD_TO_DEG = 180.0f / PI;
}
