#pragma once
#include "Config.h"
#include <cmath>
#include <type_traits>

namespace Math
{

	// ─── Trigonometric functions (scalar only) ────────────────────────────

	template <typename T> inline T Sin(T x) { return static_cast<T>(std::sin(static_cast<float>(x))); }
	template <typename T> inline T Cos(T x) { return static_cast<T>(std::cos(static_cast<float>(x))); }
	template <typename T> inline T Tan(T x) { return static_cast<T>(std::tan(static_cast<float>(x))); }
	template <typename T> inline T Cot(T x) { return static_cast<T>(1.0f / std::tan(static_cast<float>(x))); }
	template <typename T> inline T Sec(T x) { return static_cast<T>(1.0f / std::cos(static_cast<float>(x))); }
	template <typename T> inline T Csc(T x) { return static_cast<T>(1.0f / std::sin(static_cast<float>(x))); }

	template <typename T> inline T Asin(T x) { return static_cast<T>(std::asin(static_cast<float>(x))); }
	template <typename T> inline T Acos(T x) { return static_cast<T>(std::acos(static_cast<float>(x))); }
	template <typename T> inline T Atan(T x) { return static_cast<T>(std::atan(static_cast<float>(x))); }
	template <typename T> inline T Acot(T x) { return static_cast<T>(1.0f / std::atan(static_cast<float>(x))); }
	template <typename T> inline T Asec(T x) { return static_cast<T>(1.0f / std::acos(static_cast<float>(x))); }
	template <typename T> inline T Acsc(T x) { return static_cast<T>(std::asin(static_cast<float>(1.0f / static_cast<float>(x)))); }

	template <typename T> inline T Atan2(T y, T x) { return static_cast<T>(std::atan2(static_cast<float>(y), static_cast<float>(x))); }

	// Hyperbolic
	template <typename T> inline T Sinh(T x) { return static_cast<T>(std::sinh(static_cast<float>(x))); }
	template <typename T> inline T Cosh(T x) { return static_cast<T>(std::cosh(static_cast<float>(x))); }
	template <typename T> inline T Tanh(T x) { return static_cast<T>(std::tanh(static_cast<float>(x))); }

	// ─── Angle conversion (scalar only) ───────────────────────────────────

	template <typename T> inline T Degrees(T x) { return static_cast<T>(static_cast<float>(x) * RAD_TO_DEG); }
	template <typename T> inline T Radians(T x) { return static_cast<T>(static_cast<float>(x) * DEG_TO_RAD); }

	// ─── Exponential / logarithmic (scalar only) ──────────────────────────

	template <typename T> inline T Sqrt(T x) { return static_cast<T>(std::sqrt(static_cast<float>(x))); }
	template <typename T> inline T InvSqrt(T x) { return static_cast<T>(1.0f / std::sqrt(static_cast<float>(x))); }
	template <typename T> inline T Pow(T x, T y) { return static_cast<T>(std::pow(static_cast<float>(x), static_cast<float>(y))); }
	template <typename T> inline T Exp(T x) { return static_cast<T>(std::exp(static_cast<float>(x))); }
	template <typename T> inline T Log(T x) { return static_cast<T>(std::log(static_cast<float>(x))); }

	// ─── Rounding (scalar only) ───────────────────────────────────────────

	template <typename T> inline T Floor(T x) { return static_cast<T>(std::floor(static_cast<float>(x))); }
	template <typename T> inline T Ceil(T x) { return static_cast<T>(std::ceil(static_cast<float>(x))); }
	template <typename T> inline T Round(T x) { return static_cast<T>(std::round(static_cast<float>(x))); }

	// Fractional part of a value (x - floor(x)) — works for scalars and vectors
	template <typename T> inline T Fract(T x) { return x - Floor(x); }

	// ─── Clamping and comparison (scalar & vector friendly) ───────────────

	// Scalar abs — uses std::abs which handles integral types correctly
	template <typename T>
	inline std::enable_if_t<std::is_arithmetic_v<T>, T> Abs(T x) { return static_cast<T>(std::abs(static_cast<float>(x))); }

	// Clamp — works for scalars and vectors (uses operator<, >)
	template <typename T> inline T Clamp(T x, T minVal, T maxVal) { return x < minVal ? minVal : (x > maxVal ? maxVal : x); }

	// Min / Max — component-wise for vectors, scalar comparison for scalars
	template <typename T> inline T Min(T x, T y) { return x < y ? x : y; }
	template <typename T> inline T Max(T x, T y) { return x > y ? x : y; }

	// Signed remainder — scalar version using std::fmod
	template <typename T>
	inline std::enable_if_t<std::is_arithmetic_v<T>, T> Mod(T x, T y) { return static_cast<T>(std::fmod(static_cast<float>(x), static_cast<float>(y))); }

	// Component-wise modulo for vectors (used by periodic noise)
	template <typename T>
	inline std::enable_if_t<!std::is_arithmetic_v<T>, T> Mod(T x, T y) { return x - Floor(x / y) * y; }

	// Step function: 0 if edge > x, 1 otherwise (GLM-style) — works for scalars and vectors
	template <typename T> inline T Step(T edge, T x) { return x >= edge ? static_cast<T>(1) : static_cast<T>(0); }

	// Smoothstep: Hermite interpolation between 0 and 1 — works for scalars and vectors
	template <typename T> inline T SmoothStep(T edge0, T edge1, T x)
	{
		T t = Clamp((x - edge0) / (edge1 - edge0), static_cast<T>(0), static_cast<T>(1));
		return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
	}

	// ─── Interpolation ────────────────────────────────────────────────────

	// Linear interpolation — works for scalars and vectors
	template <typename T> inline T Lerp(T a, T b, T t) { return a + (b - a) * t; }

	// Spherical linear interpolation for scalars degenerates to lerp
	template <typename T> inline T Slerp(T a, T b, T t) { return Lerp(a, b, t); }

	// ─── Utility ──────────────────────────────────────────────────────────

	// Floating-point equality check with tolerance
	inline float InError(float val, float target, float error) { return (val >= target - error) && (val <= target + error); }

} // namespace Math
