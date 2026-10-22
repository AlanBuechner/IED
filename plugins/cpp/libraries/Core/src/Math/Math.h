#pragma once

// --- Core configuration and constants -------------------------------------
#include "Config.h"

// --- Scalar math utilities (trig, rounding, clamping, interpolation) ------
#include "MathFunctions.h"

// --- Vector / Matrix / Quaternion types -----------------------------------
#include "Vector.h"
#include "Matrix.h"
#include "MatrixFunctions.h"
#include "Quaternion.h"

// --- Geometric algebra types (bivector, rotor, motor, plane) --------------
#include "Bivector.h"
#include "GeometricAlgebra.h"

// --- Backward-compatible type aliases -------------------------------------
#include "Types.h"

// --- SIMD-optimized routines (conditionally compiled) ---------------------
#ifdef MATH_SIMD_SSE41
	#include "SIMD/VectorSIMD.h"
	#include "SIMD/MatrixSIMD.h"
	#include "SIMD/QuaternionSIMD.h"
#endif

#ifdef MATH_SIMD_AVX2
	#include "SIMD/VectorSIMD.h"
	#include "SIMD/MatrixSIMD.h"
	#include "SIMD/QuaternionSIMD.h"
#endif

// --- Noise functions (non-inline implementations in Math.cpp) -------------
namespace Math
{
	float Perlin(float p);
	float Perlin(const Vector2F& p);
	float Perlin(const Vector3F& p);
	float Perlin(const Vector4F& p);

	float Perlin(float p, float rep);
	float Perlin(const Vector2F& p, const Vector2F& rep);
	float Perlin(const Vector3F& p, const Vector3F& rep);
	float Perlin(const Vector4F& p, const Vector4F& rep);

	float Simplex(float p);
	float Simplex(const Vector2F& p);
	float Simplex(const Vector3F& p);
	float Simplex(const Vector4F& p);
}
