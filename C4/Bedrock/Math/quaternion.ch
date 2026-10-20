// Quat: a rotation as a unit quaternion (x, y, z, w; w last in memory), packed so its
// arithmetic is SIMD. `a * b` rotates by b, then a; `q * v` rotates a vector. The angle and matrix conversions
// follow GLM, so values match it exactly. Euler angles are YXZ, the same rotation RotationYXZ builds.
//     Math::Quat turn = Math::AngleAxis(Math::Radians(90.0f), Math::FVector3::Up);
//     Math::FVector3 facing = turn * Math::FVector3::Forward;
//     Math::Mat4 rotation = Math::ToMat4(turn);

include "matrix.ch"

namespace Math
{
	class Quat
	{
	public:
		public union { packed float x, y, z, w; packed float[4] data; };

		[operator("create")] public void Init() { data = { 0.0f, 0.0f, 0.0f, 1.0f }; }   // the identity
		[operator("create")] public void Init(float px, float py, float pz, float pw) { data = { px, py, pz, pw }; }
		[operator("create")] public void Init(FVector3 v, float pw) { data = { v.x, v.y, v.z, pw }; }
		[operator("create")] public void Init(packed float[4] lanes) { data = lanes; }

		[operator("*")] public Quat Mul(Quat o) const;
		[operator("*")] public FVector3 Rotate(FVector3 v) const;
		[operator("*")] public Quat Scale(float s) const { return Quat(data * s); }
		[operator("+")] public Quat Add(Quat o) const { return Quat(data + o.data); }
		[operator("-")] public Quat Sub(Quat o) const { return Quat(data - o.data); }
		[operator("negate")] public Quat Negate() const { return Quat(-data); }
		[operator("==")] public bool Equals(Quat o) const { packed bool[4] same = data == o.data; return same[0] && same[1] && same[2] && same[3]; }
		[operator("!=")] public bool NotEquals(Quat o) const { return !Equals(o); }

		public FVector3 Vector() const { return FVector3(xyz); }
		public Quat Conjugate() const { return Quat(-x, -y, -z, w); }
		// The conjugate over the squared length: the inverse rotation even when not unit length. Zero gives the identity.
		public Quat Inverse() const;
		public Quat Normalized() const;

		public static const Quat Identity = { 0.0f, 0.0f, 0.0f, 1.0f };
	}

	float Dot(Quat a, Quat b);
	float Length(Quat q);
	// A rotation of `angle` radians about `axis` (normalized here).
	Quat AngleAxis(float angle, FVector3 axis);
	// The rotation's angle in radians, from 0 to 2 pi, and its axis (+X for the identity).
	float Angle(Quat q);
	FVector3 Axis(Quat q);
	// From Euler angles in radians (x pitch, y yaw, z roll): yaw * pitch * roll, so roll turns first. GLM's
	// quat(vec3) uses another order (roll * yaw * pitch); this one agrees with RotationYXZ.
	Quat FromEuler(FVector3 radians);
	// Back to Euler angles, pitch in [-pi/2, pi/2]. At gimbal lock (pitch +-pi/2) yaw and roll turn about one axis,
	// so roll is 0 and yaw holds the whole turn.
	FVector3 ToEuler(Quat q);
	FVector3 ToEuler(const Mat3& rotation);
	float Pitch(Quat q) { return ToEuler(q).x; }
	float Yaw(Quat q) { return ToEuler(q).y; }
	float Roll(Quat q) { return ToEuler(q).z; }
	// Component-wise, not normalized; t is not clamped.
	Quat Lerp(Quat a, Quat b, float t);
	// Along the shorter arc at constant speed; nearly equal rotations fall back to Lerp.
	Quat Slerp(Quat a, Quat b, float t);
	Mat3 ToMat3(Quat q);
	Mat4 ToMat4(Quat q);
	// The rotation in a matrix's upper-left 3x3 (which must have no scale).
	Quat FromMat3(const Mat3& m);
	Quat FromMat4(const Mat4& m);
	bool NearlyEqual(Quat a, Quat b, float epsilon);
}
