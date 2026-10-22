#pragma once
#include "Core/Types.h"
#include "Config.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace Math
{
	// ─── Forward declarations (needed by SwizzleProxy conversion operators) ──
	template <typename T> struct Vector2;
	template <typename T> struct Vector3;
	template <typename T> struct Vector4;

	// ─── Swizzle proxy (read-only HLSL-like swizzles) ──────────────────────
	// 2-component swizzle
	template <typename T, int I0, int I1>
	struct SwizzleProxy2
	{
		const T* m_Data;
		constexpr T operator[](int i) const { return (i == 0) ? m_Data[I0] : m_Data[I1]; }
		constexpr operator Vector2<std::remove_const_t<T>>() const { return { m_Data[I0], m_Data[I1] }; }
	};

	// 3-component swizzle
	template <typename T, int I0, int I1, int I2>
	struct SwizzleProxy3
	{
		const T* m_Data;
		constexpr T operator[](int i) const
		{
			if (i == 0) return m_Data[I0];
			if (i == 1) return m_Data[I1];
			return m_Data[I2];
		}
		constexpr operator Vector3<std::remove_const_t<T>>() const { return { m_Data[I0], m_Data[I1], m_Data[I2] }; }
	};

	// 4-component swizzle
	template <typename T, int I0, int I1, int I2, int I3>
	struct SwizzleProxy4
	{
		const T* m_Data;
		constexpr T operator[](int i) const
		{
			if (i == 0) return m_Data[I0];
			if (i == 1) return m_Data[I1];
			if (i == 2) return m_Data[I2];
			return m_Data[I3];
		}
		constexpr operator Vector4<std::remove_const_t<T>>() const { return { m_Data[I0], m_Data[I1], m_Data[I2], m_Data[I3] }; }
	};

	// ─── Vector2 (default T = float for backward compatibility) ─────────────
	template<typename T = float>
	struct Vector2
	{
		union
		{
			struct { T x, y; };
			struct { T r, g; };
			struct { T data[2]; };
		};

		constexpr Vector2() : x(0), y(0) {}
		template<typename U, typename std::enable_if_t<std::is_arithmetic_v<U> && !std::is_enum_v<U>, int> = 0>
		constexpr Vector2(U v) : data{ static_cast<T>(v), static_cast<T>(v) } {}
		template<typename U, typename V> constexpr Vector2(U px, V py) : x(static_cast<T>(px)), y(static_cast<T>(py)) {}
		template<typename U> constexpr Vector2(const Vector2<U>& other)
			: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

		// Downcast from larger vectors
		template<typename U> constexpr Vector2(const Vector3<U>& other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}
		template<typename U> constexpr Vector2(const Vector4<U>& other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

		constexpr T& operator[](int i) { return data[i]; }
		constexpr const T& operator[](int i) const { return data[i]; }

		constexpr bool operator==(const Vector2& other) const
		{
			for (int i = 0; i < 2; ++i)
				if (data[i] != other.data[i]) return false;
			return true;
		}
		constexpr bool operator!=(const Vector2& other) const { return !(*this == other); }

		// Swizzles
		constexpr auto xx() const { return SwizzleProxy2<const T, 0, 0>{ data }; }
		constexpr auto xy() const { return SwizzleProxy2<const T, 0, 1>{ data }; }
		constexpr auto yx() const { return SwizzleProxy2<const T, 1, 0>{ data }; }
		constexpr auto yy() const { return SwizzleProxy2<const T, 1, 1>{ data }; }

		// Arithmetic
		constexpr Vector2 operator-() const { return { -x, -y }; }
		constexpr Vector2 operator+(const Vector2& o) const { return { x + o.x, y + o.y }; }
		constexpr Vector2 operator-(const Vector2& o) const { return { x - o.x, y - o.y }; }
		constexpr Vector2 operator*(T s) const { return { x * s, y * s }; }
		constexpr Vector2 operator/(T s) const { return { x / s, y / s }; }
		constexpr Vector2 operator*(const Vector2& o) const { return { x * o.x, y * o.y }; }
		constexpr Vector2 operator/(const Vector2& o) const { return { x / o.x, y / o.y }; }

		constexpr Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
		constexpr Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }
		constexpr Vector2& operator*=(T s) { x *= s; y *= s; return *this; }
		constexpr Vector2& operator/=(T s) { x /= s; y /= s; return *this; }

		// Static constants
		static constexpr Vector2 Zero() { return { 0, 0 }; }
		static constexpr Vector2 One() { return { 1, 1 }; }
	};

	// ─── Vector3 (default T = float for backward compatibility) ─────────────
	template<typename T = float>
	struct Vector3
	{
		union
		{
			struct { T x, y, z; };
			struct { T r, g, b; };
			struct { T data[3]; };
		};

		constexpr Vector3() : x(0), y(0), z(0) {}
		template<typename U, typename std::enable_if_t<std::is_arithmetic_v<U> && !std::is_enum_v<U>, int> = 0>
		constexpr Vector3(U v) : data{ static_cast<T>(v), static_cast<T>(v), static_cast<T>(v) } {}
		template<typename U, typename V, typename W> constexpr Vector3(U px, V py, W pz) : x(static_cast<T>(px)), y(static_cast<T>(py)), z(static_cast<T>(pz)) {}
		template<typename U> constexpr Vector3(const Vector3<U>& other)
			: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

		// Downcast from larger vectors
		template<typename U> constexpr Vector3(const Vector4<U>& other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

		// Construct from Vector2 + w
		template<typename U, typename V> constexpr Vector3(const Vector2<U>& v, V pz) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(pz)) {}

		constexpr T& operator[](int i) { return data[i]; }
		constexpr const T& operator[](int i) const { return data[i]; }

		constexpr bool operator==(const Vector3& other) const
		{
			for (int i = 0; i < 3; ++i)
				if (data[i] != other.data[i]) return false;
			return true;
		}
		constexpr bool operator!=(const Vector3& other) const { return !(*this == other); }

		// Swizzles (2-component)
		constexpr auto xx() const { return SwizzleProxy2<const T, 0, 0>{ data }; }
		constexpr auto xy() const { return SwizzleProxy2<const T, 0, 1>{ data }; }
		constexpr auto xz() const { return SwizzleProxy2<const T, 0, 2>{ data }; }
		constexpr auto yx() const { return SwizzleProxy2<const T, 1, 0>{ data }; }
		constexpr auto yy() const { return SwizzleProxy2<const T, 1, 1>{ data }; }
		constexpr auto yz() const { return SwizzleProxy2<const T, 1, 2>{ data }; }
		constexpr auto zx() const { return SwizzleProxy2<const T, 2, 0>{ data }; }
		constexpr auto zy() const { return SwizzleProxy2<const T, 2, 1>{ data }; }
		constexpr auto zz() const { return SwizzleProxy2<const T, 2, 2>{ data }; }

		// Swizzles (3-component)
		constexpr auto xxx() const { return SwizzleProxy3<const T, 0, 0, 0>{ data }; }
		constexpr auto xyz() const { return SwizzleProxy3<const T, 0, 1, 2>{ data }; }
		constexpr auto xzy() const { return SwizzleProxy3<const T, 0, 2, 1>{ data }; }
		constexpr auto yxx() const { return SwizzleProxy3<const T, 1, 0, 0>{ data }; }
		constexpr auto yxy() const { return SwizzleProxy3<const T, 1, 0, 1>{ data }; }
		constexpr auto yyx() const { return SwizzleProxy3<const T, 1, 1, 0>{ data }; }
		constexpr auto yyz() const { return SwizzleProxy3<const T, 1, 1, 2>{ data }; }
		constexpr auto yzx() const { return SwizzleProxy3<const T, 1, 2, 0>{ data }; }
		constexpr auto yzy() const { return SwizzleProxy3<const T, 1, 2, 1>{ data }; }
		constexpr auto yzz() const { return SwizzleProxy3<const T, 1, 2, 2>{ data }; }
		constexpr auto zxx() const { return SwizzleProxy3<const T, 2, 0, 0>{ data }; }
		constexpr auto zxy() const { return SwizzleProxy3<const T, 2, 0, 1>{ data }; }
		constexpr auto zxz() const { return SwizzleProxy3<const T, 2, 0, 2>{ data }; }
		constexpr auto zyx() const { return SwizzleProxy3<const T, 2, 1, 0>{ data }; }
		constexpr auto zyy() const { return SwizzleProxy3<const T, 2, 1, 1>{ data }; }
		constexpr auto zyz() const { return SwizzleProxy3<const T, 2, 1, 2>{ data }; }
		constexpr auto zzx() const { return SwizzleProxy3<const T, 2, 2, 0>{ data }; }
		constexpr auto zzy() const { return SwizzleProxy3<const T, 2, 2, 1>{ data }; }
		constexpr auto zzz() const { return SwizzleProxy3<const T, 2, 2, 2>{ data }; }

		// Arithmetic
		constexpr Vector3 operator-() const { return { -x, -y, -z }; }
		constexpr Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		constexpr Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		constexpr Vector3 operator*(T s) const { return { x * s, y * s, z * s }; }
		constexpr Vector3 operator/(T s) const { return { x / s, y / s, z / s }; }
		constexpr Vector3 operator*(const Vector3& o) const { return { x * o.x, y * o.y, z * o.z }; }
		constexpr Vector3 operator/(const Vector3& o) const { return { x / o.x, y / o.y, z / o.z }; }

		constexpr Vector3& operator+=(const Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		constexpr Vector3& operator-=(const Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		constexpr Vector3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
		constexpr Vector3& operator/=(T s) { x /= s; y /= s; z /= s; return *this; }

		// Static constants
		static constexpr Vector3 Zero() { return { 0, 0, 0 }; }
		static constexpr Vector3 One() { return { 1, 1, 1 }; }
		static constexpr Vector3 UnitX() { return { 1, 0, 0 }; }
		static constexpr Vector3 UnitY() { return { 0, 1, 0 }; }
		static constexpr Vector3 UnitZ() { return { 0, 0, 1 }; }

		static constexpr Vector3 Up() { return { 0, 1, 0 }; }
		static constexpr Vector3 Down() { return { 0, -1, 0 }; }

		static constexpr Vector3 Right() { return { 1, 0, 0 }; }
		static constexpr Vector3 Left() { return { -1, 0, 0 }; }

		static constexpr Vector3 Forward() { return { 0, 0, 1 }; }
		static constexpr Vector3 Backward() { return { 0, 0, -1 }; }

	};

	// ─── Vector4 (default T = float for backward compatibility) ─────────────
	template<typename T = float>
	struct Vector4
	{
		union
		{
			struct { T x, y, z, w; };
			struct { T r, g, b, a; };
			struct { T data[4]; };
		};

		constexpr Vector4() : x(0), y(0), z(0), w(0) {}
		template<typename U, typename std::enable_if_t<std::is_arithmetic_v<U> && !std::is_enum_v<U>, int> = 0>
		constexpr Vector4(U v) : data{ static_cast<T>(v), static_cast<T>(v), static_cast<T>(v), static_cast<T>(v) } {}
		template<typename U, typename V, typename W, typename X> constexpr Vector4(U px, V py, W pz, X pw) : x(static_cast<T>(px)), y(static_cast<T>(py)), z(static_cast<T>(pz)), w(static_cast<T>(pw)) {}
		template<typename U> constexpr Vector4(const Vector4<U>& other)
			: x(static_cast<T>(other.x)), y(static_cast<T>(other.y)),
			  z(static_cast<T>(other.z)), w(static_cast<T>(other.w)) {}

		// Construct from smaller vectors
		template<typename U, typename V, typename W> constexpr Vector4(const Vector2<U>& v, V pz, W pw) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(pz)), w(static_cast<T>(pw)) {}
		template<typename U, typename V> constexpr Vector4(const Vector2<U>& v, const Vector2<V>& v2) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v2.x)), w(static_cast<T>(v2.y)) {}
		template<typename U, typename V> constexpr Vector4(const Vector3<U>& v, V pw) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)), w(static_cast<T>(pw)) {}

		constexpr T& operator[](int i) { return data[i]; }
		constexpr const T& operator[](int i) const { return data[i]; }

		constexpr bool operator==(const Vector4& other) const
		{
			for (int i = 0; i < 4; ++i)
				if (data[i] != other.data[i]) return false;
			return true;
		}
		constexpr bool operator!=(const Vector4& other) const { return !(*this == other); }

		// Swizzles (2-component) — only a representative set
		constexpr auto xx() const { return SwizzleProxy2<const T, 0, 0>{ data }; }
		constexpr auto xy() const { return SwizzleProxy2<const T, 0, 1>{ data }; }
		constexpr auto xz() const { return SwizzleProxy2<const T, 0, 2>{ data }; }
		constexpr auto xw() const { return SwizzleProxy2<const T, 0, 3>{ data }; }
		constexpr auto yx() const { return SwizzleProxy2<const T, 1, 0>{ data }; }
		constexpr auto yy() const { return SwizzleProxy2<const T, 1, 1>{ data }; }
		constexpr auto yz() const { return SwizzleProxy2<const T, 1, 2>{ data }; }
		constexpr auto yw() const { return SwizzleProxy2<const T, 1, 3>{ data }; }
		constexpr auto zx() const { return SwizzleProxy2<const T, 2, 0>{ data }; }
		constexpr auto zy() const { return SwizzleProxy2<const T, 2, 1>{ data }; }
		constexpr auto zz() const { return SwizzleProxy2<const T, 2, 2>{ data }; }
		constexpr auto zw() const { return SwizzleProxy2<const T, 2, 3>{ data }; }
		constexpr auto wx() const { return SwizzleProxy2<const T, 3, 0>{ data }; }
		constexpr auto wy() const { return SwizzleProxy2<const T, 3, 1>{ data }; }
		constexpr auto wz() const { return SwizzleProxy2<const T, 3, 2>{ data }; }
		constexpr auto ww() const { return SwizzleProxy2<const T, 3, 3>{ data }; }

		// Swizzles (3-component) — representative set
		constexpr auto xyz() const { return SwizzleProxy3<const T, 0, 1, 2>{ data }; }
		constexpr auto xyw() const { return SwizzleProxy3<const T, 0, 1, 3>{ data }; }
		constexpr auto xzw() const { return SwizzleProxy3<const T, 0, 2, 3>{ data }; }
		constexpr auto yzw() const { return SwizzleProxy3<const T, 1, 2, 3>{ data }; }

		// Swizzles (4-component) — representative set
		constexpr auto xxxx() const { return SwizzleProxy4<const T, 0, 0, 0, 0>{ data }; }
		constexpr auto yyyy() const { return SwizzleProxy4<const T, 1, 1, 1, 1>{ data }; }
		constexpr auto zzzz() const { return SwizzleProxy4<const T, 2, 2, 2, 2>{ data }; }
		constexpr auto wwww() const { return SwizzleProxy4<const T, 3, 3, 3, 3>{ data }; }
		constexpr auto xyzw() const { return SwizzleProxy4<const T, 0, 1, 2, 3>{ data }; }
		constexpr auto zyxw() const { return SwizzleProxy4<const T, 2, 1, 0, 3>{ data }; }

		// Arithmetic
		constexpr Vector4 operator-() const { return { -x, -y, -z, -w }; }
		constexpr Vector4 operator+(const Vector4& o) const { return { x + o.x, y + o.y, z + o.z, w + o.w }; }
		constexpr Vector4 operator-(const Vector4& o) const { return { x - o.x, y - o.y, z - o.z, w - o.w }; }
		constexpr Vector4 operator*(T s) const { return { x * s, y * s, z * s, w * s }; }
		constexpr Vector4 operator/(T s) const { return { x / s, y / s, z / s, w / s }; }
		constexpr Vector4 operator*(const Vector4& o) const { return { x * o.x, y * o.y, z * o.z, w * o.w }; }
		constexpr Vector4 operator/(const Vector4& o) const { return { x / o.x, y / o.y, z / o.z, w / o.w }; }

		constexpr Vector4& operator+=(const Vector4& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
		constexpr Vector4& operator-=(const Vector4& o) { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
		constexpr Vector4& operator*=(T s) { x *= s; y *= s; z *= s; w *= s; return *this; }
		constexpr Vector4& operator/=(T s) { x /= s; y /= s; z /= s; w /= s; return *this; }

		// Static constants
		static constexpr Vector4 Zero() { return { 0, 0, 0, 0 }; }
		static constexpr Vector4 One() { return { 1, 1, 1, 1 }; }
		static constexpr Vector4 UnitX() { return { 1, 0, 0, 0 }; }
		static constexpr Vector4 UnitY() { return { 0, 1, 0, 0 }; }
		static constexpr Vector4 UnitZ() { return { 0, 0, 1, 0 }; }
		static constexpr Vector4 UnitW() { return { 0, 0, 0, 1 }; }
	};

	// ─── Scalar multiplication (s * v) ──────────────────────────────────────
	template <typename T> constexpr Vector2<T> operator*(T s, const Vector2<T>& v) { return v * s; }
	template <typename T> constexpr Vector3<T> operator*(T s, const Vector3<T>& v) { return v * s; }
	template <typename T> constexpr Vector4<T> operator*(T s, const Vector4<T>& v) { return v * s; }

	// ─── Dot product ────────────────────────────────────────────────────────
	template <typename T> constexpr T Dot(const Vector2<T>& a, const Vector2<T>& b)
	{ return a.x * b.x + a.y * b.y; }
	template <typename T> constexpr T Dot(const Vector3<T>& a, const Vector3<T>& b)
	{ return a.x * b.x + a.y * b.y + a.z * b.z; }
	template <typename T> constexpr T Dot(const Vector4<T>& a, const Vector4<T>& b)
	{ return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

	// ─── Cross product ──────────────────────────────────────────────────────
	template <typename T> constexpr Vector3<T> Cross(const Vector3<T>& a, const Vector3<T>& b)
	{
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
	}

	template <typename T> constexpr T Cross(const Vector2<T>& a, const Vector2<T>& b)
	{ return a.x * b.y - b.x * a.y; }

	// ─── Length / Distance ──────────────────────────────────────────────────
	template <typename T> T LengthSqr(const Vector2<T>& v) { return Dot(v, v); }
	template <typename T> T LengthSqr(const Vector3<T>& v) { return Dot(v, v); }
	template <typename T> T LengthSqr(const Vector4<T>& v) { return Dot(v, v); }

	template <typename T> T Length(const Vector2<T>& v) { return std::sqrt(LengthSqr(v)); }
	template <typename T> T Length(const Vector3<T>& v) { return std::sqrt(LengthSqr(v)); }
	template <typename T> T Length(const Vector4<T>& v) { return std::sqrt(LengthSqr(v)); }

	template <typename T> T DistanceSqr(const Vector2<T>& a, const Vector2<T>& b) { return LengthSqr(b - a); }
	template <typename T> T DistanceSqr(const Vector3<T>& a, const Vector3<T>& b) { return LengthSqr(b - a); }
	template <typename T> T DistanceSqr(const Vector4<T>& a, const Vector4<T>& b) { return LengthSqr(b - a); }

	template <typename T> T Distance(const Vector2<T>& a, const Vector2<T>& b) { return Length(b - a); }
	template <typename T> T Distance(const Vector3<T>& a, const Vector3<T>& b) { return Length(b - a); }
	template <typename T> T Distance(const Vector4<T>& a, const Vector4<T>& b) { return Length(b - a); }

	// ─── Normalize ──────────────────────────────────────────────────────────
	template <typename T> Vector2<T> Normalized(const Vector2<T>& v)
	{ float l = 1.0f / static_cast<float>(Length(v)); return v * static_cast<T>(l); }
	template <typename T> Vector3<T> Normalized(const Vector3<T>& v)
	{ float l = 1.0f / static_cast<float>(Length(v)); return v * static_cast<T>(l); }
	template <typename T> Vector4<T> Normalized(const Vector4<T>& v)
	{ float l = 1.0f / static_cast<float>(Length(v)); return v * static_cast<T>(l); }

	// ─── Lerp ───────────────────────────────────────────────────────────────
	template <typename T> constexpr Vector2<T> Lerp(const Vector2<T>& a, const Vector2<T>& b, T t)
	{ return a + (b - a) * t; }
	template <typename T> constexpr Vector3<T> Lerp(const Vector3<T>& a, const Vector3<T>& b, T t)
	{ return a + (b - a) * t; }
	template <typename T> constexpr Vector4<T> Lerp(const Vector4<T>& a, const Vector4<T>& b, T t)
	{ return a + (b - a) * t; }

	// ─── Clamp ──────────────────────────────────────────────────────────────
	template <typename T> constexpr Vector2<T> Clamp(const Vector2<T>& v, T mn, T mx)
	{
		return { v.x < mn ? mn : (v.x > mx ? mx : v.x), v.y < mn ? mn : (v.y > mx ? mx : v.y) };
	}
	template <typename T> constexpr Vector3<T> Clamp(const Vector3<T>& v, T mn, T mx)
	{
		return { v.x < mn ? mn : (v.x > mx ? mx : v.x), v.y < mn ? mn : (v.y > mx ? mx : v.y),
			 v.z < mn ? mn : (v.z > mx ? mx : v.z) };
	}
	template <typename T> constexpr Vector4<T> Clamp(const Vector4<T>& v, T mn, T mx)
	{
		return { v.x < mn ? mn : (v.x > mx ? mx : v.x), v.y < mn ? mn : (v.y > mx ? mx : v.y),
			 v.z < mn ? mn : (v.z > mx ? mx : v.z), v.w < mn ? mn : (v.w > mx ? mx : v.w) };
	}

	// ─── Min / Max ──────────────────────────────────────────────────────────
	template <typename T> constexpr Vector2<T> Min(const Vector2<T>& a, const Vector2<T>& b)
	{ return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y }; }
	template <typename T> constexpr Vector3<T> Min(const Vector3<T>& a, const Vector3<T>& b)
	{ return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z }; }
	template <typename T> constexpr Vector4<T> Min(const Vector4<T>& a, const Vector4<T>& b)
	{ return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z, a.w < b.w ? a.w : b.w }; }

	template <typename T> constexpr Vector2<T> Max(const Vector2<T>& a, const Vector2<T>& b)
	{ return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y }; }
	template <typename T> constexpr Vector3<T> Max(const Vector3<T>& a, const Vector3<T>& b)
	{ return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z }; }
	template <typename T> constexpr Vector4<T> Max(const Vector4<T>& a, const Vector4<T>& b)
	{ return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z, a.w > b.w ? a.w : b.w }; }

	// ─── Degrees / Radians (vector overloads) ──────────────────────────────
	template <typename T> constexpr Vector2<T> Degrees(Vector2<T> rad)
	{ return { rad.x * RAD_TO_DEG, rad.y * RAD_TO_DEG }; }
	template <typename T> constexpr Vector3<T> Degrees(Vector3<T> rad)
	{ return { rad.x * RAD_TO_DEG, rad.y * RAD_TO_DEG, rad.z * RAD_TO_DEG }; }

	template <typename T> constexpr Vector2<T> Radians(Vector2<T> deg)
	{ return { deg.x * DEG_TO_RAD, deg.y * DEG_TO_RAD }; }
	template <typename T> constexpr Vector3<T> Radians(Vector3<T> deg)
	{ return { deg.x * DEG_TO_RAD, deg.y * DEG_TO_RAD, deg.z * DEG_TO_RAD }; }

	// ─── Spherical / Cartesian conversions ──────────────────────────────────
	inline Vector3<float> SphericalToCartesian(Vector2<float> rot)
	{
		float c = std::cos(rot.y);
		return { std::cos(rot.x) * c, std::sin(rot.y), std::sin(rot.x) * c };
	}

	inline Vector2<float> CartesianToSpherical(Vector3<float> dir)
	{
		dir = Normalized(dir);
		float pitch = static_cast<float>(std::asin(dir.y));
		float yaw = std::atan2(dir.z, dir.x);
		return Degrees(Vector2<float>(pitch, yaw));
	}

	// ─── Abs ────────────────────────────────────────────────────────────────
	template <typename T> constexpr Vector2<T> Abs(const Vector2<T>& v) { return { std::abs(v.x), std::abs(v.y) }; }
	template <typename T> constexpr Vector3<T> Abs(const Vector3<T>& v) { return { std::abs(v.x), std::abs(v.y), std::abs(v.z) }; }
	template <typename T> constexpr Vector4<T> Abs(const Vector4<T>& v) { return { std::abs(v.x), std::abs(v.y), std::abs(v.z), std::abs(v.w) }; }

	// ─── Floor / Ceil ──────────────────────────────────────────────────────
	template <typename T> constexpr Vector2<T> Floor(const Vector2<T>& v) { return { std::floor(v.x), std::floor(v.y) }; }
	template <typename T> constexpr Vector3<T> Floor(const Vector3<T>& v) { return { std::floor(v.x), std::floor(v.y), std::floor(v.z) }; }
	template <typename T> constexpr Vector4<T> Floor(const Vector4<T>& v) { return { std::floor(v.x), std::floor(v.y), std::floor(v.z), std::floor(v.w) }; }

	template <typename T> constexpr Vector2<T> Ceil(const Vector2<T>& v) { return { std::ceil(v.x), std::ceil(v.y) }; }
	template <typename T> constexpr Vector3<T> Ceil(const Vector3<T>& v) { return { std::ceil(v.x), std::ceil(v.y), std::ceil(v.z) }; }
	template <typename T> constexpr Vector4<T> Ceil(const Vector4<T>& v) { return { std::ceil(v.x), std::ceil(v.y), std::ceil(v.z), std::ceil(v.w) }; }

	// ─── Type aliases for backward compatibility ───────────────────────────
	using Vector2F = Vector2<float>;
	using Vector3F = Vector3<float>;
	using Vector4F = Vector4<float>;

	using IVector2F = Vector2<int32>;
	using IVector3F = Vector3<int32>;
	using IVector4F = Vector4<int32>;

	using UVector2F = Vector2<uint32>;
	using UVector3F = Vector3<uint32>;
	using UVector4F = Vector4<uint32>;
}
