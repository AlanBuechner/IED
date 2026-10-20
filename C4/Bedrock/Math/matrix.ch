// Matrices: Mat2, Mat3 and Mat4 of floats, column-major like GLM - `columns[c]` is column c, so
// `m[c]` is a column vector and `m.At(row, column)` one element. A matrix times a vector transforms the vector;
// a product `a * b` applies b first, then a. Each column is a packed vector, so products are SIMD.
//     Math::Mat4 world = Math::Translation(position) * Math::Rotation(angle, Math::FVector3::Up);
//     Math::FVector4 p = world * Math::FVector4(local, 1.0f);

include "vector.ch"

namespace Math
{
	class Mat2
	{
	public:
		public FVector2[2] columns;

		[operator("create")] public void Init() {}   // every element zero
		[operator("create")] public void Init(float diagonal);
		[operator("create")] public void Init(FVector2 c0, FVector2 c1) { columns[0] = c0; columns[1] = c1; }

		[operator("index")] public FVector2& Column(int64 c) { return columns[c]; }
		[operator("index")] public const FVector2& Column(int64 c) const { return columns[c]; }
		public float At(int64 row, int64 column) const { return columns[column][row]; }
		public void Set(int64 row, int64 column, float value) { columns[column][row] = value; }

		[operator("*")] public Mat2 Mul(const Mat2& o) const { return Mat2(Transform(o.columns[0]), Transform(o.columns[1])); }
		[operator("*")] public FVector2 Transform(FVector2 v) const { return FVector2(columns[0].data * v.x + columns[1].data * v.y); }
		[operator("*")] public Mat2 Scale(float s) const { return Mat2(columns[0] * s, columns[1] * s); }
		[operator("+")] public Mat2 Add(const Mat2& o) const { return Mat2(columns[0] + o.columns[0], columns[1] + o.columns[1]); }
		[operator("-")] public Mat2 Sub(const Mat2& o) const { return Mat2(columns[0] - o.columns[0], columns[1] - o.columns[1]); }
		[operator("==")] public bool Equals(const Mat2& o) const { return columns[0] == o.columns[0] && columns[1] == o.columns[1]; }
		[operator("!=")] public bool NotEquals(const Mat2& o) const { return !Equals(o); }

		public float Determinant() const { return columns[0].x * columns[1].y - columns[1].x * columns[0].y; }
		public Mat2 Transposed() const { return Mat2(FVector2(columns[0].x, columns[1].x), FVector2(columns[0].y, columns[1].y)); }
		// The inverse; a singular matrix (determinant 0) gives infinities, as in GLM.
		public Mat2 Inverse() const;

		public static const Mat2 Identity = { FVector2(1.0f, 0.0f), FVector2(0.0f, 1.0f) };
		public static const Mat2 Zero = { FVector2(0.0f), FVector2(0.0f) };
	}

	class Mat3
	{
	public:
		public FVector3[3] columns;

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(float diagonal);
		[operator("create")] public void Init(FVector3 c0, FVector3 c1, FVector3 c2) { columns[0] = c0; columns[1] = c1; columns[2] = c2; }

		[operator("index")] public FVector3& Column(int64 c) { return columns[c]; }
		[operator("index")] public const FVector3& Column(int64 c) const { return columns[c]; }
		public float At(int64 row, int64 column) const { return columns[column][row]; }
		public void Set(int64 row, int64 column, float value) { columns[column][row] = value; }

		[operator("*")] public Mat3 Mul(const Mat3& o) const { return Mat3(Transform(o.columns[0]), Transform(o.columns[1]), Transform(o.columns[2])); }
		[operator("*")] public FVector3 Transform(FVector3 v) const { return FVector3(columns[0].data * v.x + columns[1].data * v.y + columns[2].data * v.z); }
		[operator("*")] public Mat3 Scale(float s) const { return Mat3(columns[0] * s, columns[1] * s, columns[2] * s); }
		[operator("+")] public Mat3 Add(const Mat3& o) const { return Mat3(columns[0] + o.columns[0], columns[1] + o.columns[1], columns[2] + o.columns[2]); }
		[operator("-")] public Mat3 Sub(const Mat3& o) const { return Mat3(columns[0] - o.columns[0], columns[1] - o.columns[1], columns[2] - o.columns[2]); }
		[operator("==")] public bool Equals(const Mat3& o) const { return columns[0] == o.columns[0] && columns[1] == o.columns[1] && columns[2] == o.columns[2]; }
		[operator("!=")] public bool NotEquals(const Mat3& o) const { return !Equals(o); }

		public float Determinant() const;
		public Mat3 Transposed() const;
		public Mat3 Inverse() const;

		public static const Mat3 Identity = { FVector3(1.0f, 0.0f, 0.0f), FVector3(0.0f, 1.0f, 0.0f), FVector3(0.0f, 0.0f, 1.0f) };
		public static const Mat3 Zero = { FVector3(0.0f), FVector3(0.0f), FVector3(0.0f) };
	}

	class Mat4
	{
	public:
		public FVector4[4] columns;

		[operator("create")] public void Init() {}
		[operator("create")] public void Init(float diagonal);
		[operator("create")] public void Init(FVector4 c0, FVector4 c1, FVector4 c2, FVector4 c3) { columns[0] = c0; columns[1] = c1; columns[2] = c2; columns[3] = c3; }
		// The 3x3 matrix in the upper left, the rest of the identity around it.
		[operator("create")] public void Init(const Mat3& m);

		[operator("index")] public FVector4& Column(int64 c) { return columns[c]; }
		[operator("index")] public const FVector4& Column(int64 c) const { return columns[c]; }
		public float At(int64 row, int64 column) const { return columns[column][row]; }
		public void Set(int64 row, int64 column, float value) { columns[column][row] = value; }
		public FVector4 Row(int64 r) const { return FVector4(columns[0][r], columns[1][r], columns[2][r], columns[3][r]); }

		[operator("*")] public Mat4 Mul(const Mat4& o) const;
		[operator("*")] public FVector4 Transform(FVector4 v) const;
		[operator("*")] public Mat4 Scale(float s) const { return Mat4(columns[0] * s, columns[1] * s, columns[2] * s, columns[3] * s); }
		[operator("+")] public Mat4 Add(const Mat4& o) const;
		[operator("-")] public Mat4 Sub(const Mat4& o) const;
		[operator("*=")] public Mat4& MulBy(const Mat4& o) { Mat4 product = Mul(o); *this = product; return *this; }
		[operator("==")] public bool Equals(const Mat4& o) const;
		[operator("!=")] public bool NotEquals(const Mat4& o) const { return !Equals(o); }

		// A point (w = 1, translated) and a direction (w = 0, not translated) through this matrix.
		public FVector3 TransformPoint(FVector3 p) const { return FVector3(Transform(FVector4(p, 1.0f))); }
		public FVector3 TransformDirection(FVector3 d) const { return FVector3(Transform(FVector4(d, 0.0f))); }

		public float Determinant() const;
		public Mat4 Transposed() const;
		// The inverse (GLM's cofactor method); a singular matrix gives infinities, as in GLM.
		public Mat4 Inverse() const;
		// The upper-left 3x3: rotation and scale without the translation.
		public Mat3 ToMat3() const { return Mat3(FVector3(columns[0]), FVector3(columns[1]), FVector3(columns[2])); }

		public static const Mat4 Identity = { FVector4(1.0f, 0.0f, 0.0f, 0.0f), FVector4(0.0f, 1.0f, 0.0f, 0.0f),
			FVector4(0.0f, 0.0f, 1.0f, 0.0f), FVector4(0.0f, 0.0f, 0.0f, 1.0f) };
		public static const Mat4 Zero = { FVector4(0.0f), FVector4(0.0f), FVector4(0.0f), FVector4(0.0f) };

	private:
		// Rows P and Q of the 2x2 minors of the last two columns that Inverse combines, four at a time.
		template<int32 P, int32 Q> private static packed float[4] InverseMinors(packed float[4] c1, packed float[4] c2, packed float[4] c3) {
			return simd_shuffle(c2, c1, P, P, P + 4, P + 4) * simd_shuffle(c3, c2, Q, Q, Q, Q + 4) -
				simd_shuffle(c3, c2, P, P, P, P + 4) * simd_shuffle(c2, c1, Q, Q, Q + 4, Q + 4);
		}
	}

	// Every element within epsilon of the other's.
	bool NearlyEqual(const Mat3& a, const Mat3& b, float epsilon);
	bool NearlyEqual(const Mat4& a, const Mat4& b, float epsilon);
}
