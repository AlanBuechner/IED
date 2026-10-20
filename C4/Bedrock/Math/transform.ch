// The matrices of transforms and cameras, left-handed (+X right, +Y up, +Z forward) with a 0 to 1 depth range
// (Direct3D, Vulkan), as GLM's ...LH_ZO functions build them. Angles are radians. Euler angles are x pitch
// (about X), y yaw (about Y) and z roll (about Z), applied roll first, then pitch, then yaw.
//     Math::Mat4 view = Math::LookAt(eye, target, Math::FVector3::Up);
//     Math::Mat4 projection = Math::Perspective(Math::Radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
//     Math::Mat4 world = Math::TransformMatrix(position, rotation, scale);

include "quaternion.ch"

namespace Math
{
	Mat4 Translation(FVector3 offset);
	Mat4 Scaling(FVector3 factors);
	// `angle` about `axis` (normalized here).
	Mat4 Rotation(float angle, FVector3 axis);
	// Yaw * pitch * roll from Euler angles (GLM's eulerAngleYXZ).
	Mat4 RotationYXZ(FVector3 radians);
	// Translation * rotation * scale: scale first, then rotate, then move.
	Mat4 TransformMatrix(FVector3 position, FVector3 rotation, FVector3 scale);
	Mat4 TransformMatrix(FVector3 position, Quat rotation, FVector3 scale);

	// A world-to-view matrix looking from `eye` toward `target`.
	Mat4 LookAt(FVector3 eye, FVector3 target, FVector3 up);
	// `verticalFov` radians top to bottom; depth `zNear` maps to 0 and `zFar` to 1.
	Mat4 Perspective(float verticalFov, float aspect, float zNear, float zFar);
	Mat4 Orthographic(float left, float right, float bottom, float top, float zNear, float zFar);

	// A transform taken apart: where it moves to, its rotation as Euler angles and its scale per axis.
	struct Transform {
		public FVector3 position;
		public FVector3 rotation;
		public FVector3 scale;
	}

	// The translation, rotation and scale a TransformMatrix built (no shear, no negative scale).
	Transform Decompose(const Mat4& m);
	FVector3 TranslationOf(const Mat4& m) { return FVector3(m.columns[3]); }
}
