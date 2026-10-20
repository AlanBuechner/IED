// Scalar math for the Math library: constants and the functions the vector, matrix and quaternion types are
// built from. Every function is a template over the number type, so `Math::Clamp(x, 0, 10)` and
// `Math::Clamp(t, 0.0f, 1.0f)` both work; the transcendental ones take float or double.

namespace Math
{
	const float kPi = 3.14159265358979323846f;
	const float kTau = 6.28318530717958647692f;
	const float kHalfPi = 1.57079632679489661923f;
	const float kE = 2.71828182845904523536f;
	const float kDegreesToRadians = kPi / 180.0f;
	const float kRadiansToDegrees = 180.0f / kPi;
	// A tolerance for comparing floats near 1: about eight ulps.
	const float kEpsilon = 0.000001f;
	// The gap between 1.0f and the next float (FLT_EPSILON).
	const float kFloatEpsilon = 0.00000011920929f;

	template<T> T Min(T a, T b) { return b < a ? b : a; }
	template<T> T Max(T a, T b) { return a < b ? b : a; }
	template<T> T Clamp(T value, T low, T high) { return value < low ? low : (high < value ? high : value); }
	template<T> T Saturate(T value) { return Clamp(value, (T)0, (T)1); }
	template<T> T Abs(T value) { return value < (T)0 ? -value : value; }
	// -1, 0 or 1.
	template<T> T Sign(T value) { return (T)((T)0 < value ? 1 : 0) - (T)(value < (T)0 ? 1 : 0); }
	// a + (b - a) * t; t is not clamped.
	template<T> T Lerp(T a, T b, T t) { return a + (b - a) * t; }
	// Where `value` lies between a and b, as a fraction (Lerp's inverse); a == b gives 0.
	template<T> T InverseLerp(T a, T b, T value) { return a == b ? (T)0 : (value - a) / (b - a); }
	// Hermite smoothing of `value` from edge0 to edge1 (GLSL smoothstep).
	template<T> T SmoothStep(T edge0, T edge1, T value) { T t = Saturate(InverseLerp(edge0, edge1, value)); return t * t * ((T)3 - (T)2 * t); }
	template<T> T Radians(T degrees) { return degrees * (T)kDegreesToRadians; }
	template<T> T Degrees(T radians) { return radians * (T)kRadiansToDegrees; }
	// |a - b| <= epsilon.
	template<T> bool NearlyEqual(T a, T b, T epsilon) { return Abs(a - b) <= epsilon; }

	float Sqrt(float x) { return sqrtf(x); }
	double Sqrt(double x) { return sqrt(x); }
	float Sin(float x) { return sinf(x); }
	double Sin(double x) { return sin(x); }
	float Cos(float x) { return cosf(x); }
	double Cos(double x) { return cos(x); }
	float Tan(float x) { return tanf(x); }
	double Tan(double x) { return tan(x); }
	float Asin(float x) { return asinf(x); }
	double Asin(double x) { return asin(x); }
	float Acos(float x) { return acosf(x); }
	double Acos(double x) { return acos(x); }
	float Atan(float x) { return atanf(x); }
	double Atan(double x) { return atan(x); }
	float Atan2(float y, float x) { return atan2f(y, x); }
	double Atan2(double y, double x) { return atan2(y, x); }
	float Pow(float x, float y) { return powf(x, y); }
	double Pow(double x, double y) { return pow(x, y); }
	float Exp(float x) { return expf(x); }
	double Exp(double x) { return exp(x); }
	float Log(float x) { return logf(x); }
	double Log(double x) { return log(x); }
	float Floor(float x) { return floorf(x); }
	double Floor(double x) { return floor(x); }
	float Ceil(float x) { return ceilf(x); }
	double Ceil(double x) { return ceil(x); }
	float Round(float x) { return roundf(x); }
	double Round(double x) { return round(x); }
	float Trunc(float x) { return truncf(x); }
	double Trunc(double x) { return trunc(x); }
	// x - floor(x), in [0, 1).
	float Fract(float x) { return x - floorf(x); }
	double Fract(double x) { return x - floor(x); }
	// x - floor(x / y) * y: the sign of y, as GLSL mod (C's % and fmod keep the sign of x).
	float Mod(float x, float y) { return x - floorf(x / y) * y; }
	double Mod(double x, double y) { return x - floor(x / y) * y; }
}
