// Batches: N vectors at once in structure-of-arrays form. Lane i of x, y and z is vector i, so each operation works
// on N vectors with one SIMD instruction per component: FVector3x8 and FVector4x8 fill an AVX register, x4 an SSE
// one, x16 an AVX-512 one (SIMD_FLOAT_LANES is the target's widest). Load reads N vectors from an array of vectors
// and Store writes them back; arrays of batches keep the data in this form between frames.
//     Math::FVector3x8 positions = Math::FVector3x8::Load(&points[i]);
//     positions += velocities * dt;
//     Math::FVector3x8 world = Math::TransformPoint(model, positions);
//     packed float[8] distances = Math::Length(world - Math::FVector3x8(eye));

include "quaternion.ch"

namespace Math
{
	// The lanes 0, stride, 2 * stride, ...: where N interleaved vectors keep one component.
	template<uint32 N> packed int32[N] StridedLanes(int32 stride);

	template<T, uint32 N> class TVector3Batch
	{
	public:
		public packed T[N] x;
		public packed T[N] y;
		public packed T[N] z;

		[operator("create")] public void Init() {}
		// Every vector of the batch is `v`.
		[operator("create")] public void Init(TVector3<T> v) { x = v.x; y = v.y; z = v.z; }
		[operator("create")] public void Init(packed T[N] px, packed T[N] py, packed T[N] pz) { x = px; y = py; z = pz; }

		[operator("+")] public TVector3Batch<T, N> Add(const TVector3Batch<T, N>& o) const { return TVector3Batch<T, N>(x + o.x, y + o.y, z + o.z); }
		[operator("-")] public TVector3Batch<T, N> Sub(const TVector3Batch<T, N>& o) const { return TVector3Batch<T, N>(x - o.x, y - o.y, z - o.z); }
		[operator("*")] public TVector3Batch<T, N> Mul(const TVector3Batch<T, N>& o) const { return TVector3Batch<T, N>(x * o.x, y * o.y, z * o.z); }
		[operator("*")] public TVector3Batch<T, N> Scale(T s) const { return TVector3Batch<T, N>(x * s, y * s, z * s); }
		// Vector i scaled by lane i of `s`.
		[operator("*")] public TVector3Batch<T, N> ScaleEach(packed T[N] s) const { return TVector3Batch<T, N>(x * s, y * s, z * s); }
		[operator("/")] public TVector3Batch<T, N> Div(T s) const { return TVector3Batch<T, N>(x / s, y / s, z / s); }
		[operator("/")] public TVector3Batch<T, N> DivEach(packed T[N] s) const { return TVector3Batch<T, N>(x / s, y / s, z / s); }
		[operator("negate")] public TVector3Batch<T, N> Negate() const { return TVector3Batch<T, N>(-x, -y, -z); }

		[operator("+=")] public TVector3Batch<T, N>& AddFrom(const TVector3Batch<T, N>& o) { x += o.x; y += o.y; z += o.z; return *this; }
		[operator("-=")] public TVector3Batch<T, N>& SubFrom(const TVector3Batch<T, N>& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		[operator("*=")] public TVector3Batch<T, N>& ScaleBy(T s) { x *= s; y *= s; z *= s; return *this; }

		// Vector i of the batch.
		public TVector3<T> Get(int64 i) const { return TVector3<T>(x[i], y[i], z[i]); }
		public void Set(int64 i, TVector3<T> v) { x[i] = v.x; y[i] = v.y; z[i] = v.z; }

		// N vectors from the array `vectors`, and back.
		public static TVector3Batch<T, N> Load(const TVector3<T>* vectors) {
			const packed int32[N] lanes = StridedLanes<N>(3);
			const T* first = (const T*)vectors;
			return TVector3Batch<T, N>(simd_gather(first, lanes), simd_gather(first + 1, lanes), simd_gather(first + 2, lanes));
		}
		public void Store(TVector3<T>* vectors) const { for (int64 i = 0; i < (int64)N; ++i) vectors[i] = Get(i); }
	}

	template<T, uint32 N> class TVector4Batch
	{
	public:
		public packed T[N] x;
		public packed T[N] y;
		public packed T[N] z;
		public packed T[N] w;

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(TVector4<T> v) { x = v.x; y = v.y; z = v.z; w = v.w; }
		[operator("create")] public void Init(packed T[N] px, packed T[N] py, packed T[N] pz, packed T[N] pw) { x = px; y = py; z = pz; w = pw; }
		// The points of `v` (w = `pw`).
		[operator("create")] public void Init(const TVector3Batch<T, N>& v, T pw) { x = v.x; y = v.y; z = v.z; w = pw; }

		[operator("+")] public TVector4Batch<T, N> Add(const TVector4Batch<T, N>& o) const { return TVector4Batch<T, N>(x + o.x, y + o.y, z + o.z, w + o.w); }
		[operator("-")] public TVector4Batch<T, N> Sub(const TVector4Batch<T, N>& o) const { return TVector4Batch<T, N>(x - o.x, y - o.y, z - o.z, w - o.w); }
		[operator("*")] public TVector4Batch<T, N> Mul(const TVector4Batch<T, N>& o) const { return TVector4Batch<T, N>(x * o.x, y * o.y, z * o.z, w * o.w); }
		[operator("*")] public TVector4Batch<T, N> Scale(T s) const { return TVector4Batch<T, N>(x * s, y * s, z * s, w * s); }
		[operator("*")] public TVector4Batch<T, N> ScaleEach(packed T[N] s) const { return TVector4Batch<T, N>(x * s, y * s, z * s, w * s); }
		[operator("negate")] public TVector4Batch<T, N> Negate() const { return TVector4Batch<T, N>(-x, -y, -z, -w); }
		[operator("+=")] public TVector4Batch<T, N>& AddFrom(const TVector4Batch<T, N>& o) { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }

		public TVector4<T> Get(int64 i) const { return TVector4<T>(x[i], y[i], z[i], w[i]); }
		public void Set(int64 i, TVector4<T> v) { x[i] = v.x; y[i] = v.y; z[i] = v.z; w[i] = v.w; }
		// The first three components of each vector.
		public TVector3Batch<T, N> Xyz() const { return TVector3Batch<T, N>(x, y, z); }

		public static TVector4Batch<T, N> Load(const TVector4<T>* vectors) {
			const packed int32[N] lanes = StridedLanes<N>(4);
			const T* first = (const T*)vectors;
			return TVector4Batch<T, N>(simd_gather(first, lanes), simd_gather(first + 1, lanes), simd_gather(first + 2, lanes), simd_gather(first + 3, lanes));
		}
		public void Store(TVector4<T>* vectors) const { for (int64 i = 0; i < (int64)N; ++i) vectors[i] = Get(i); }
	}

	alias FVector3x4 = TVector3Batch<float, 4>;
	alias FVector3x8 = TVector3Batch<float, 8>;
	alias FVector3x16 = TVector3Batch<float, 16>;
	alias FVector4x4 = TVector4Batch<float, 4>;
	alias FVector4x8 = TVector4Batch<float, 8>;
	alias FVector4x16 = TVector4Batch<float, 16>;

	// --- per vector, N at a time (float and double lanes) ------------------------------------------------
	template<T, uint32 N> packed T[N] Dot(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) {
		return simd_muladd(a.z, b.z, simd_muladd(a.y, b.y, a.x * b.x));
	}
	template<T, uint32 N> packed T[N] Dot(const TVector4Batch<T, N>& a, const TVector4Batch<T, N>& b) {
		return simd_muladd(a.w, b.w, simd_muladd(a.z, b.z, simd_muladd(a.y, b.y, a.x * b.x)));
	}
	// a x b; left-handed, as Cross of two vectors.
	template<T, uint32 N> TVector3Batch<T, N> Cross(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) {
		return TVector3Batch<T, N>(simd_muladd(a.y, b.z, -(a.z * b.y)), simd_muladd(a.z, b.x, -(a.x * b.z)), simd_muladd(a.x, b.y, -(a.y * b.x)));
	}
	template<T, uint32 N> packed T[N] LengthSquared(const TVector3Batch<T, N>& v) { return Dot(v, v); }
	template<T, uint32 N> packed T[N] Length(const TVector3Batch<T, N>& v) { return simd_sqrt(Dot(v, v)); }
	template<T, uint32 N> packed T[N] Distance(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) { return Length(b - a); }
	// Each vector scaled to length 1; a zero vector stays zero.
	template<T, uint32 N> TVector3Batch<T, N> Normalized(const TVector3Batch<T, N>& v);
	template<T, uint32 N> TVector3Batch<T, N> Lerp(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b, T t) { return a + (b - a) * t; }
	template<T, uint32 N> TVector3Batch<T, N> Min(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) {
		return TVector3Batch<T, N>(simd_min(b.x, a.x), simd_min(b.y, a.y), simd_min(b.z, a.z));
	}
	template<T, uint32 N> TVector3Batch<T, N> Max(const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) {
		return TVector3Batch<T, N>(simd_max(b.x, a.x), simd_max(b.y, a.y), simd_max(b.z, a.z));
	}
	// Vector i from `a` where mask lane i is true, else from `b`.
	template<T, uint32 N> TVector3Batch<T, N> Select(packed bool[N] mask, const TVector3Batch<T, N>& a, const TVector3Batch<T, N>& b) {
		return TVector3Batch<T, N>(simd_select(mask, a.x, b.x), simd_select(mask, a.y, b.y), simd_select(mask, a.z, b.z));
	}

	// --- N vectors through one transform ------------------------------------------------------------------
	// Each vector (with its own w) through `m`.
	template<uint32 N> TVector4Batch<float, N> TransformVector(const Mat4& m, const TVector4Batch<float, N>& v);
	// Points (w = 1, translated) and directions (w = 0) through `m`.
	template<uint32 N> TVector3Batch<float, N> TransformPoint(const Mat4& m, const TVector3Batch<float, N>& p);
	template<uint32 N> TVector3Batch<float, N> TransformDirection(const Mat4& m, const TVector3Batch<float, N>& d);
	// Each vector rotated by `q` (a unit quaternion), as q * v.
	template<uint32 N> TVector3Batch<float, N> Rotate(Quat q, const TVector3Batch<float, N>& v);
}
