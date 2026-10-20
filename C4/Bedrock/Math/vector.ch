// Vectors: TVector2<T>, TVector3<T> and TVector4<T> over packed lanes, so a vector lives in a SIMD register and
// its arithmetic is SIMD. x/y/z/w, r/g/b/a and `data` (the whole vector, a `packed T[N]`) view the same lanes,
// and every swizzle run works (`v.zyx`, `v.xy = w.yx`). Operators are component-wise, `*` and `/` included;
// Dot, Cross, Length and the rest are free functions. FVector3 is the float vector, IVector3 / UVector3 /
// DVector3 the int32, uint32 and double ones.
//     Math::FVector3 up = Math::FVector3::Up;
//     Math::FVector3 v = Math::FVector3(1.0f, 2.0f, 2.0f);
//     float length = Math::Length(v);                       // 3
//     Math::FVector3 side = Math::Cross(v, up) * 0.5f;

include "scalar.ch"

namespace Math
{
	template<T> class TVector3;
	template<T> class TVector4;

	template<T> class TVector2
	{
	public:
		public union { packed T x, y; packed T r, g; packed T[2] data; };

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(T s) { data = { s, s }; }
		[operator("create")] public void Init(T px, T py) { data = { px, py }; }
		[operator("create")] public void Init(packed T[2] lanes) { data = lanes; }
		[operator("create")] public void Init(TVector3<T> v) { data = v.xy; }   // the first two lanes
		[operator("create")] public void Init(TVector4<T> v) { data = v.xy; }

		[operator("+")] public TVector2<T> Add(TVector2<T> o) const { return TVector2<T>(data + o.data); }
		[operator("-")] public TVector2<T> Sub(TVector2<T> o) const { return TVector2<T>(data - o.data); }
		[operator("*")] public TVector2<T> Mul(TVector2<T> o) const { return TVector2<T>(data * o.data); }
		[operator("*")] public TVector2<T> Scale(T s) const { return TVector2<T>(data * s); }
		[operator("/")] public TVector2<T> Div(TVector2<T> o) const { return TVector2<T>(data / o.data); }
		[operator("/")] public TVector2<T> Div(T s) const { return TVector2<T>(data / s); }
		[operator("negate")] public TVector2<T> Negate() const { return TVector2<T>(-data); }

		[operator("+=")] public TVector2<T>& AddFrom(TVector2<T> o) { data += o.data; return *this; }
		[operator("-=")] public TVector2<T>& SubFrom(TVector2<T> o) { data -= o.data; return *this; }
		[operator("*=")] public TVector2<T>& MulBy(TVector2<T> o) { data *= o.data; return *this; }
		[operator("*=")] public TVector2<T>& ScaleBy(T s) { data *= s; return *this; }
		[operator("/=")] public TVector2<T>& DivBy(TVector2<T> o) { data /= o.data; return *this; }
		[operator("/=")] public TVector2<T>& DivBy(T s) { data /= s; return *this; }

		[operator("==")] public bool Equals(TVector2<T> o) const { return simd_all(data == o.data); }
		[operator("!=")] public bool NotEquals(TVector2<T> o) const { return !Equals(o); }
		[operator("index")] public T Lane(int64 i) const { return data[i]; }
		[operator("index_assign")] public void SetLane(int64 i, T value) { data[i] = value; }

		public static const TVector2<T> Zero = { (T)0, (T)0 };
		public static const TVector2<T> One = { (T)1, (T)1 };
		public static const TVector2<T> UnitX = { (T)1, (T)0 };
		public static const TVector2<T> UnitY = { (T)0, (T)1 };
		public static const TVector2<T> Right = { (T)1, (T)0 };
		public static const TVector2<T> Left = { (T)-1, (T)0 };
		public static const TVector2<T> Up = { (T)0, (T)1 };
		public static const TVector2<T> Down = { (T)0, (T)-1 };
	}

	template<T> class TVector3
	{
	public:
		public union { packed T x, y, z; packed T r, g, b; packed T[3] data; };

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(T s) { data = { s, s, s }; }
		[operator("create")] public void Init(T px, T py, T pz) { data = { px, py, pz }; }
		[operator("create")] public void Init(packed T[3] lanes) { data = lanes; }
		[operator("create")] public void Init(TVector2<T> v, T pz) { data = { v.x, v.y, pz }; }
		[operator("create")] public void Init(TVector4<T> v) { data = v.xyz; }   // the first three lanes

		[operator("+")] public TVector3<T> Add(TVector3<T> o) const { return TVector3<T>(data + o.data); }
		[operator("-")] public TVector3<T> Sub(TVector3<T> o) const { return TVector3<T>(data - o.data); }
		[operator("*")] public TVector3<T> Mul(TVector3<T> o) const { return TVector3<T>(data * o.data); }
		[operator("*")] public TVector3<T> Scale(T s) const { return TVector3<T>(data * s); }
		[operator("/")] public TVector3<T> Div(TVector3<T> o) const { return TVector3<T>(data / o.data); }
		[operator("/")] public TVector3<T> Div(T s) const { return TVector3<T>(data / s); }
		[operator("negate")] public TVector3<T> Negate() const { return TVector3<T>(-data); }

		[operator("+=")] public TVector3<T>& AddFrom(TVector3<T> o) { data += o.data; return *this; }
		[operator("-=")] public TVector3<T>& SubFrom(TVector3<T> o) { data -= o.data; return *this; }
		[operator("*=")] public TVector3<T>& MulBy(TVector3<T> o) { data *= o.data; return *this; }
		[operator("*=")] public TVector3<T>& ScaleBy(T s) { data *= s; return *this; }
		[operator("/=")] public TVector3<T>& DivBy(TVector3<T> o) { data /= o.data; return *this; }
		[operator("/=")] public TVector3<T>& DivBy(T s) { data /= s; return *this; }

		[operator("==")] public bool Equals(TVector3<T> o) const { return simd_all(data == o.data); }
		[operator("!=")] public bool NotEquals(TVector3<T> o) const { return !Equals(o); }
		[operator("index")] public T Lane(int64 i) const { return data[i]; }
		[operator("index_assign")] public void SetLane(int64 i, T value) { data[i] = value; }

		public static const TVector3<T> Zero = { (T)0, (T)0, (T)0 };
		public static const TVector3<T> One = { (T)1, (T)1, (T)1 };
		public static const TVector3<T> UnitX = { (T)1, (T)0, (T)0 };
		public static const TVector3<T> UnitY = { (T)0, (T)1, (T)0 };
		public static const TVector3<T> UnitZ = { (T)0, (T)0, (T)1 };
		public static const TVector3<T> Right = { (T)1, (T)0, (T)0 };
		public static const TVector3<T> Left = { (T)-1, (T)0, (T)0 };
		public static const TVector3<T> Up = { (T)0, (T)1, (T)0 };
		public static const TVector3<T> Down = { (T)0, (T)-1, (T)0 };
		public static const TVector3<T> Forward = { (T)0, (T)0, (T)1 };    // left-handed: +Z points into the screen
		public static const TVector3<T> Backward = { (T)0, (T)0, (T)-1 };
	}

	template<T> class TVector4
	{
	public:
		public union { packed T x, y, z, w; packed T r, g, b, a; packed T[4] data; };

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(T s) { data = { s, s, s, s }; }
		[operator("create")] public void Init(T px, T py, T pz, T pw) { data = { px, py, pz, pw }; }
		[operator("create")] public void Init(packed T[4] lanes) { data = lanes; }
		[operator("create")] public void Init(TVector3<T> v, T pw) { data = { v.x, v.y, v.z, pw }; }
		[operator("create")] public void Init(TVector2<T> v, T pz, T pw) { data = { v.x, v.y, pz, pw }; }
		[operator("create")] public void Init(TVector2<T> low, TVector2<T> high) { data = { low.x, low.y, high.x, high.y }; }

		[operator("+")] public TVector4<T> Add(TVector4<T> o) const { return TVector4<T>(data + o.data); }
		[operator("-")] public TVector4<T> Sub(TVector4<T> o) const { return TVector4<T>(data - o.data); }
		[operator("*")] public TVector4<T> Mul(TVector4<T> o) const { return TVector4<T>(data * o.data); }
		[operator("*")] public TVector4<T> Scale(T s) const { return TVector4<T>(data * s); }
		[operator("/")] public TVector4<T> Div(TVector4<T> o) const { return TVector4<T>(data / o.data); }
		[operator("/")] public TVector4<T> Div(T s) const { return TVector4<T>(data / s); }
		[operator("negate")] public TVector4<T> Negate() const { return TVector4<T>(-data); }

		[operator("+=")] public TVector4<T>& AddFrom(TVector4<T> o) { data += o.data; return *this; }
		[operator("-=")] public TVector4<T>& SubFrom(TVector4<T> o) { data -= o.data; return *this; }
		[operator("*=")] public TVector4<T>& MulBy(TVector4<T> o) { data *= o.data; return *this; }
		[operator("*=")] public TVector4<T>& ScaleBy(T s) { data *= s; return *this; }
		[operator("/=")] public TVector4<T>& DivBy(TVector4<T> o) { data /= o.data; return *this; }
		[operator("/=")] public TVector4<T>& DivBy(T s) { data /= s; return *this; }

		[operator("==")] public bool Equals(TVector4<T> o) const { return simd_all(data == o.data); }
		[operator("!=")] public bool NotEquals(TVector4<T> o) const { return !Equals(o); }
		[operator("index")] public T Lane(int64 i) const { return data[i]; }
		[operator("index_assign")] public void SetLane(int64 i, T value) { data[i] = value; }

		public static const TVector4<T> Zero = { (T)0, (T)0, (T)0, (T)0 };
		public static const TVector4<T> One = { (T)1, (T)1, (T)1, (T)1 };
		public static const TVector4<T> UnitX = { (T)1, (T)0, (T)0, (T)0 };
		public static const TVector4<T> UnitY = { (T)0, (T)1, (T)0, (T)0 };
		public static const TVector4<T> UnitZ = { (T)0, (T)0, (T)1, (T)0 };
		public static const TVector4<T> UnitW = { (T)0, (T)0, (T)0, (T)1 };
	}

	alias FVector2 = TVector2<float>;
	alias FVector3 = TVector3<float>;
	alias FVector4 = TVector4<float>;
	alias DVector2 = TVector2<double>;
	alias DVector3 = TVector3<double>;
	alias DVector4 = TVector4<double>;
	alias IVector2 = TVector2<int32>;
	alias IVector3 = TVector3<int32>;
	alias IVector4 = TVector4<int32>;
	alias UVector2 = TVector2<uint32>;
	alias UVector3 = TVector3<uint32>;
	alias UVector4 = TVector4<uint32>;
	alias Vector2 = FVector2;
	alias Vector3 = FVector3;
	alias Vector4 = FVector4;

	// --- products and lengths ---------------------------------------------------------------------------
	// The products summed in simd_reduce_add's halving order: (x + z) + (y + w).
	template<T> T Dot(TVector2<T> a, TVector2<T> b) { return simd_reduce_add(a.data * b.data); }
	template<T> T Dot(TVector3<T> a, TVector3<T> b) { return simd_reduce_add(a.data * b.data); }
	template<T> T Dot(TVector4<T> a, TVector4<T> b) { return simd_reduce_add(a.data * b.data); }
	// a x b; left-handed coordinates make Cross(Right, Up) == Forward.
	template<T> TVector3<T> Cross(TVector3<T> a, TVector3<T> b) { return TVector3<T>(a.yzx * b.zxy - a.zxy * b.yzx); }
	// The z of the 3D cross product of (a, 0) and (b, 0): positive when b is counter-clockwise from a.
	template<T> T Cross(TVector2<T> a, TVector2<T> b) { return a.x * b.y - a.y * b.x; }
	template<T> T LengthSquared(TVector2<T> v) { return Dot(v, v); }
	template<T> T LengthSquared(TVector3<T> v) { return Dot(v, v); }
	template<T> T LengthSquared(TVector4<T> v) { return Dot(v, v); }
	template<T> T Length(TVector2<T> v) { return Sqrt(Dot(v, v)); }
	template<T> T Length(TVector3<T> v) { return Sqrt(Dot(v, v)); }
	template<T> T Length(TVector4<T> v) { return Sqrt(Dot(v, v)); }
	template<T> T DistanceSquared(TVector2<T> a, TVector2<T> b) { return LengthSquared(b - a); }
	template<T> T DistanceSquared(TVector3<T> a, TVector3<T> b) { return LengthSquared(b - a); }
	template<T> T DistanceSquared(TVector4<T> a, TVector4<T> b) { return LengthSquared(b - a); }
	template<T> T Distance(TVector2<T> a, TVector2<T> b) { return Length(b - a); }
	template<T> T Distance(TVector3<T> a, TVector3<T> b) { return Length(b - a); }
	template<T> T Distance(TVector4<T> a, TVector4<T> b) { return Length(b - a); }
	// v scaled to length 1; the zero vector stays zero.
	template<T> TVector2<T> Normalized(TVector2<T> v);
	template<T> TVector3<T> Normalized(TVector3<T> v);
	template<T> TVector4<T> Normalized(TVector4<T> v);
	// The angle between two vectors, in radians, from 0 to pi.
	template<T> T Angle(TVector3<T> a, TVector3<T> b);
	// v mirrored about the plane whose unit normal is `normal` (GLSL reflect).
	template<T> TVector3<T> Reflect(TVector3<T> v, TVector3<T> normal) { return v - normal * ((T)2 * Dot(v, normal)); }
	// The part of v along `onto` (which need not be unit length; a zero `onto` gives zero).
	template<T> TVector3<T> Project(TVector3<T> v, TVector3<T> onto);

	// --- component-wise -------------------------------------------------------------------------------
	// Min(a, b) is `b < a ? b : a` per lane, as the scalar Min (and std::min): simd_min with the operands swapped.
	template<T> TVector2<T> Min(TVector2<T> a, TVector2<T> b) { return TVector2<T>(simd_min(b.data, a.data)); }
	template<T> TVector3<T> Min(TVector3<T> a, TVector3<T> b) { return TVector3<T>(simd_min(b.data, a.data)); }
	template<T> TVector4<T> Min(TVector4<T> a, TVector4<T> b) { return TVector4<T>(simd_min(b.data, a.data)); }
	template<T> TVector2<T> Max(TVector2<T> a, TVector2<T> b) { return TVector2<T>(simd_max(b.data, a.data)); }
	template<T> TVector3<T> Max(TVector3<T> a, TVector3<T> b) { return TVector3<T>(simd_max(b.data, a.data)); }
	template<T> TVector4<T> Max(TVector4<T> a, TVector4<T> b) { return TVector4<T>(simd_max(b.data, a.data)); }
	template<T> TVector2<T> Clamp(TVector2<T> v, T low, T high) { return Min(Max(v, TVector2<T>(low)), TVector2<T>(high)); }
	template<T> TVector3<T> Clamp(TVector3<T> v, T low, T high) { return Min(Max(v, TVector3<T>(low)), TVector3<T>(high)); }
	template<T> TVector4<T> Clamp(TVector4<T> v, T low, T high) { return Min(Max(v, TVector4<T>(low)), TVector4<T>(high)); }
	// The sign cleared in each lane (-0 becomes 0).
	template<T> TVector2<T> Abs(TVector2<T> v) { return TVector2<T>(simd_abs(v.data)); }
	template<T> TVector3<T> Abs(TVector3<T> v) { return TVector3<T>(simd_abs(v.data)); }
	template<T> TVector4<T> Abs(TVector4<T> v) { return TVector4<T>(simd_abs(v.data)); }
	template<T> TVector2<T> Lerp(TVector2<T> a, TVector2<T> b, T t) { return a + (b - a) * t; }
	template<T> TVector3<T> Lerp(TVector3<T> a, TVector3<T> b, T t) { return a + (b - a) * t; }
	template<T> TVector4<T> Lerp(TVector4<T> a, TVector4<T> b, T t) { return a + (b - a) * t; }
	template<T> TVector2<T> Floor(TVector2<T> v) { return TVector2<T>(simd_floor(v.data)); }
	template<T> TVector3<T> Floor(TVector3<T> v) { return TVector3<T>(simd_floor(v.data)); }
	template<T> TVector4<T> Floor(TVector4<T> v) { return TVector4<T>(simd_floor(v.data)); }
	template<T> TVector2<T> Ceil(TVector2<T> v) { return TVector2<T>(simd_ceil(v.data)); }
	template<T> TVector3<T> Ceil(TVector3<T> v) { return TVector3<T>(simd_ceil(v.data)); }
	template<T> TVector4<T> Ceil(TVector4<T> v) { return TVector4<T>(simd_ceil(v.data)); }
	// Halves away from zero, as the scalar Round.
	template<T> TVector2<T> Round(TVector2<T> v) { return TVector2<T>(simd_round(v.data)); }
	template<T> TVector3<T> Round(TVector3<T> v) { return TVector3<T>(simd_round(v.data)); }
	template<T> TVector4<T> Round(TVector4<T> v) { return TVector4<T>(simd_round(v.data)); }
	template<T> TVector2<T> Fract(TVector2<T> v) { return v - Floor(v); }
	template<T> TVector3<T> Fract(TVector3<T> v) { return v - Floor(v); }
	template<T> TVector4<T> Fract(TVector4<T> v) { return v - Floor(v); }
	template<T> TVector2<T> Radians(TVector2<T> degrees) { return degrees * (T)kDegreesToRadians; }
	template<T> TVector3<T> Radians(TVector3<T> degrees) { return degrees * (T)kDegreesToRadians; }
	template<T> TVector2<T> Degrees(TVector2<T> radians) { return radians * (T)kRadiansToDegrees; }
	template<T> TVector3<T> Degrees(TVector3<T> radians) { return radians * (T)kRadiansToDegrees; }
	// Every lane within epsilon of the other's.
	template<T> bool NearlyEqual(TVector2<T> a, TVector2<T> b, T epsilon) { return simd_all(simd_abs(a.data - b.data) <= epsilon); }
	template<T> bool NearlyEqual(TVector3<T> a, TVector3<T> b, T epsilon) { return simd_all(simd_abs(a.data - b.data) <= epsilon); }
	template<T> bool NearlyEqual(TVector4<T> a, TVector4<T> b, T epsilon) { return simd_all(simd_abs(a.data - b.data) <= epsilon); }
}
