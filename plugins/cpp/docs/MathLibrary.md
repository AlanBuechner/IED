# Math Library

The Core library's math is a native C++20 implementation providing vectors, matrices, quaternions, and scalar math utilities. It lives in `plugins/cpp/libraries/Core/src/Math/` and is part of the Core static library, making it available to every project that links Core.

All public types and functions are in the `Math` namespace.

## Source Files

| File | Purpose |
|------|---------|
| `Config.h` | Constants (PI, TAU, EPSILON), coordinate system defines, SIMD detection |
| `Types.h` | Type aliases (IVector2/3/4, UVector2/3/4, Mat2x2/3x3/4x4) for backward compatibility |
| `Math.h` / `Math.cpp` | Umbrella header, scalar math functions, Perlin/Simplex noise generators |
| `MathFunctions.h` | Scalar math utilities (trig, rounding, clamping, interpolation) |
| `Vector.h` | Vector2/3/4 template classes with operators and methods |
| `Matrix.h` | Mat2/3/4 classes with construction, inversion, decomposition |
| `MatrixFunctions.h` | Matrix utility functions (determinant, trace, transpose helpers) |
| `Quaternion.h` | Quaternion class with operations and matrix casting |
| `Bivector.h` | Bivector types for geometric algebra |
| `GeometricAlgebra.h` | Geometric algebra operations |
| `SIMD/Intrinsics.h` | SIMD instruction detection (SSE4.1, AVX2) |
| `SIMD/VectorSIMD.h` | SIMD-optimized vector operations with scalar fallbacks |
| `SIMD/MatrixSIMD.h` | SIMD-optimized matrix operations with scalar fallbacks |
| `SIMD/QuaternionSIMD.h` | SIMD-optimized quaternion operations with scalar fallbacks |

---

## Configuration

The math library is configured through `Config.h`:

```cpp
constexpr float PI      = 3.141592653589793f;
constexpr float TAU     = PI * 2;
constexpr float E       = 2.718281828459045f;
constexpr float EPSILON = 1e-10f;
```

### SIMD Support

The library conditionally enables SIMD optimizations based on compile-time macros:

| Macro | Effect |
|-------|--------|
| `MATH_SIMD_SSE41` | Enables SSE4.1 vectorized operations |
| `MATH_SIMD_AVX2` | Enables AVX2 vectorized operations |

When SIMD is unavailable, all operations fall back to scalar implementations. Target machines running Windows 10+ support SSE4.1; AVX2 is optional with automatic fallback.

### Coordinate System

The math library uses a **left-handed coordinate system** with depth range `[0, 1]`, matching the DirectX 12 backend convention.

---

## Type Aliases

`Types.h` defines backward-compatible aliases so existing code works without modification:

| Alias | Underlying Type | Typical Use |
|-------|----------------|-------------|
| `Math::IVector2/3/4` | `IVector2F/3F/4F` | Viewport resolution, grid indices |
| `Math::UVector2/3/4` | `UVector2F/3F/4F` | Texture dimensions |
| `Math::Mat2x2/3x3/4x4` | `Mat2/3/4` | Transform matrices |

### Vector Types

Vectors are template classes (`Vector2<T>`, `Vector3<T>`, `Vector4<T>`) with common specializations:

| Type | Template | Use |
|------|----------|-----|
| `Vector2F/3F/4F` | `Vector<N<float>>` | Positions, directions, colors |
| `IVector2F/3F/4F` | `Vector<N<int>>` | Integer coordinates, indices |
| `UVector2F/3F/4F` | `Vector<N<uint>>` | Unsigned dimensions, counts |

Vectors support element access via `.x`, `.y`, `.z`, `.w` members and array indexing `[i]`. They implement full operator overloads (`+`, `-`, `*`, `/`, `==`, `!=`).

### Matrix Types

Matrices are column-major classes designed for shader uniform compatibility:

| Type | Columns x Rows | Use |
|------|---------------|-----|
| `Mat2` | 2x2 | 2D transforms |
| `Mat3` | 3x3 | Normal matrices, rotation-only |
| `Mat4` | 4x4 | Model/view/projection transforms |

Matrices provide `.data()` for raw pointer access (shader uploads) and column/element access via `[]` operators.

### Quaternion

The `Quaternion` class stores `(x, y, z, w)` components with the scalar part in `w`. It supports construction from angle-axis, matrix casting, and interpolation.

---

## Scalar Math (`MathFunctions.h`)

Constants and templated scalar functions:

### Trigonometry

| Function | Description |
|----------|-------------|
| `Sin(x)`, `Cos(x)`, `Tan(x)` | Basic trigonometric functions |
| `Cot(x)`, `Sec(x)`, `Csc(x)` | Reciprocal trigonometric functions |
| `Asin(x)`, `Acos(x)`, `Atan(x)` | Inverse trigonometric functions |
| `Acot(x)`, `Asec(x)`, `Acsc(x)` | Inverse reciprocal trigonometric functions |
| `Atan2(y, x)` | Two-argument arctangent |
| `Sinh(x)`, `Cosh(x)`, `Tanh(x)` | Hyperbolic functions |

### Angle Conversion

| Function | Description |
|----------|-------------|
| `Degrees(x)` | Radians -> degrees |
| `Radians(x)` | Degrees -> radians |

### Arithmetic Helpers

| Function | Description |
|----------|-------------|
| `Sqrt(x)`, `InvSqrt(x)` | Square root and reciprocal square root |
| `Pow(x, y)` | Power function |
| `Exp(x)`, `Log(x)` | Exponential and natural logarithm |
| `Floor(x)`, `Ceil(x)`, `Round(x)` | Rounding functions |
| `Fract(x)` | Fractional part |
| `Abs(x)` | Absolute value |
| `Clamp(x, min, max)` | Clamp to range |
| `Min(x, y)`, `Max(x, y)` | Minimum / maximum |
| `Mod(x, y)` | Modulo |
| `Step(edge, x)`, `SmoothStep(edge0, edge1, x)` | Step functions |

### Interpolation

| Function | Description |
|----------|-------------|
| `Lerp(a, b, t)` | Linear interpolation between `a` and `b` |
| `Slerp(a, b, t)` | Spherical linear interpolation (quaternions) |

### Noise Generators

Perlin and Simplex noise functions for scalar, 2D, 3D, and 4D inputs with optional repetition:

```cpp
float Perlin(float p);
float Perlin(const Vector2F& p);
float Perlin(const Vector3F& p);
float Perlin(const Vector4F& p);

float Perlin(float p, float rep);
float Perlin(const Vector2F& p, const Vector2F& rep);
// ... etc. for 3D and 4D

float Simplex(float p);
float Simplex(const Vector2F& p);
float Simplex(const Vector3F& p);
float Simplex(const Vector4F& p);
```

---

## Vector Operations (`Vector.h`)

### Length and Distance

| Function | Return | Description |
|----------|--------|-------------|
| `Length(v)` | `float` | Euclidean length of vector |
| `LengthSqr(v)` | `float` | Squared length (avoids sqrt) |
| `Distance(v1, v2)` | `float` | Distance between two points |
| `DistanceSqr(v1, v2)` | `float` | Squared distance |

All are overloaded for `Vector2`, `Vector3`, and `Vector4`.

### Dot Product

```cpp
float Dot(const Vector2F& v1, const Vector2F& v2);
float Dot(const Vector3F& v1, const Vector3F& v2);
float Dot(const Vector4F& v1, const Vector4F& v2);
```

Returns the scalar dot product. Used for angle checks, projections, and lighting calculations.

### Cross Product

```cpp
Vector3F Cross(const Vector3F& v1, const Vector3F& v2);  // returns perpendicular vector
float    Cross(const Vector2F& v1, const Vector2F& v2);  // returns scalar (z-component)
```

The 3D cross product returns a `Vector3F` orthogonal to both inputs. The 2D variant returns the signed magnitude (the z-component of the 3D cross).

### Normalization

| Function | Return | Description |
|----------|--------|-------------|
| `Normalized(v)` | New vector | Returns a unit-length copy |
| `Normalize(v&)` | Reference | Normalizes in-place, returns the same reference |

Both are overloaded for `Vector2`, `Vector3`, and `Vector4`.

### Linear Interpolation

```cpp
Vector2F Lerp(const Vector2F& v1, const Vector2F& v2, float a);
Vector3F Lerp(const Vector3F& v1, const Vector3F& v2, float a);
Vector4F Lerp(const Vector4F& v1, const Vector4F& v2, float a);
```

Interpolates between two vectors by factor `a in [0, 1]`.

### Coordinate Conversions

| Function | Description |
|----------|-------------|
| `SphericalToCartesian(Vector2F rot)` | Converts spherical angles (yaw, pitch in degrees) to a unit `Vector3F` direction |
| `CartesianToSpherical(Vector3F dir)` | Converts a `Vector3F` direction back to spherical angles |

### Vector Angle Conversion

```cpp
Vector2F Degrees(Vector2F rad);
Vector2F Radians(Vector2F deg);
Vector3F Degrees(Vector3F rad);
Vector3F Radians(Vector3F deg);
```

Per-component degree/radian conversion for vectors.

---

## Matrix Operations (`Matrix.h`)

### Construction Functions

| Function | Description |
|----------|-------------|
| `Ortho(left, right, bottom, top, zNear, zFar)` | Orthographic projection matrix |
| `Perspective(fovy, aspect, zNear, zFar)` | Perspective projection matrix (left-handed, depth 0-1) |
| `LookAt(eye, center, up)` | View matrix from camera position |
| `Rotate(angle, axis)` | Rotation matrix around an arbitrary axis |
| `Scale(scale)` | Scaling matrix from a `Vector3F` |
| `Translate(position)` | Translation matrix from a `Vector3F` |

### Inversion

```cpp
Mat2 Inverse(const Mat2& mat);
Mat3 Inverse(const Mat3& mat);
Mat4 Inverse(const Mat4& mat);
```

Native implementation using cofactor/adjugate method. Used extensively for view matrices, parent-space transforms, and unprojection.

### Transform Decomposition

```cpp
bool DecomposeTransform(const Mat4& transform,
                        Vector3F& outPosition,
                        Vector3F& outRotation,
                        Vector3F& outScale);
```

Decomposes a `Mat4` into its translation (position), rotation (Euler angles in radians), and scale components. Returns `false` if the matrix is singular or contains perspective components that cannot be cleanly decomposed. Used by `TransformComponent::SetGlobalTransformFromMatrix`.

### Euler Angle Helpers

```cpp
Mat4 EulerAngleYXZ(float pitch, float yaw, float roll);
```

Builds a rotation matrix from Euler angles in YXZ order (the library's convention).

### Value Pointer Helpers

```cpp
const float* ValuePtr(const Mat2& m);
float* ValuePtr(Mat2& m);
// Overloads for Mat3 and Mat4
```

Returns a raw pointer to the matrix data for shader uniform uploads. Replaces `glm::value_ptr`.

---

## Quaternion Operations (`Quaternion.h`)

### Construction

| Function | Description |
|----------|-------------|
| `AngleAxis(angle, axis)` | Create quaternion from rotation angle (radians) and axis |
| `QuatCast(mat3)` / `QuatCast(mat4)` | Extract quaternion from a rotation matrix |

### Arithmetic & Properties

| Function | Return | Description |
|----------|--------|-------------|
| `Angle(q)` | `float` | Rotation angle in radians |
| `Axis(q)` | `Vector3F` | Rotation axis (unit vector) |
| `Length(q)` | `float` | Quaternion magnitude |
| `Dot(q1, q2)` | `float` | Scalar dot product of two quaternions |
| `Inverse(q)` | `Quaternion` | Multiplicative inverse |
| `Conjugate(q)` | `Quaternion` | Conjugate (negates imaginary part) |
| `Abs(q)` | `Quaternion` | Ensures positive w component |
| `Normalize(q)` | `Quaternion` | Returns unit quaternion |

### Interpolation

| Function | Description |
|----------|-------------|
| `Mix(q1, q2, a)` | Linear mix |
| `Lerp(q1, q2, a)` | Linear interpolation between two quaternions |
| `Slerp(q1, q2, a)` | Spherical linear interpolation - constant-speed rotation |

### Euler Angle Extraction

| Function | Return | Description |
|----------|--------|-------------|
| `EulerAngles(q)` | `Vector3F` | Pitch, yaw, roll in radians |
| `Pitch(q)` | `float` | X-axis rotation |
| `Yaw(q)` | `float` | Y-axis rotation |
| `Roll(q)` | `float` | Z-axis rotation |

### Composition & Casting

| Function | Description |
|----------|-------------|
| `Rotate(q, angle, axis)` | Compose an additional rotation onto a quaternion |
| `Mat3Cast(q)` | Convert quaternion to a 3x3 rotation matrix |
| `Mat4Cast(q)` | Convert quaternion to a 4x4 rotation matrix |

---

## Geometric Algebra (`GeometricAlgebra.h`, `Bivector.h`)

The library includes a geometric algebra module for advanced spatial computations, providing Rotor, Motor, Bivector, and Plane types built on the native vector/matrix/quaternion foundation.

### Bivector (`Bivector.h`)

A grade-2 element of 3D spatial geometric algebra (Cl(3,0) extended with translational bivectors for motor/dual-quaternion use). Represents an oriented plane segment with 6 independent components:

| Component | Basis Blades | Meaning |
|-----------|-------------|---------|
| `spatial` | e12, e23, e31 | Rotational (plane) components |
| `translational` | e01, e02, e03 | Translational components |

#### Construction

```cpp
Bivector();                                        // Zero-initialized
Bivector(const Vector3F& spatial, const Vector3F& translational);
Bivector(float xy, float yz, float zx, float tx, float ty, float tz);
static constexpr Bivector Zero();                  // Static zero factory
```

#### Arithmetic Operators

| Operator | Return | Description |
|----------|--------|-------------|
| `b1 + b2` | `Bivector` | Component-wise addition |
| `b1 - b2` | `Bivector` | Component-wise subtraction |
| `-b` | `Bivector` | Negation |
| `b * s` / `s * b` | `Bivector` | Scalar multiplication |

#### Magnitude and Normalization

```cpp
float Magnitude() const;          // Euclidean magnitude across all 6 components
Bivector Normalized() const;      // Unit bivector (returns Zero if magnitude < 1e-9)
constexpr float Dot(const Bivector& o) const;  // Inner product (scalar part of geometric product)
```

#### Namespace-Level Utilities

```cpp
float Magnitude(const Bivector& b);
Bivector Normalized(const Bivector& b);
bool operator==(const Bivector& a, const Bivector& b);
bool operator!=(const Bivector& a, const Bivector& b);
```

### Rotor

A rotor is an even-grade multivector encoding a 3D rotation. It maps bijectively to a unit quaternion:

| Rotor Field | Quaternion Equivalent | Meaning |
|-------------|----------------------|---------|
| `scalar` | `w` | Even-grade scalar component |
| `spatial.x` (e12) | `z` | Rotation about Z axis |
| `spatial.y` (e23) | `x` | Rotation about X axis |
| `spatial.z` (e31) | `y` | Rotation about Y axis |

#### Construction

```cpp
constexpr Rotor();                                         // Identity rotor {1, 0}
constexpr Rotor(float scalar, const Vector3F& spatial);
static constexpr Rotor Identity();                         // Static identity factory
static Rotor FromAxisAngle(float angle, const Vector3F& axis);  // From angle (radians) and axis
```

#### Quaternion Roundtrip

```cpp
constexpr Quaternion ToQuaternion() const;
static constexpr Rotor FromQuaternion(const Quaternion& q);
```

#### Matrix Conversion

```cpp
Mat3 ToMat3() const;   // Via quaternion -> Mat3Cast
Mat4 ToMat4() const;   // Via quaternion -> Mat4Cast
```

#### Composition and Inversion

```cpp
constexpr Rotor operator*(const Rotor& o) const;  // Compose rotations: this o other (via quaternion multiplication)
constexpr Rotor Inverse() const;                  // Negates spatial part (for unit rotor, inverse == conjugate)
constexpr Rotor Conjugate() const;                // Same as Inverse for rotors
Rotor Normalize() const;                          // Normalizes to unit magnitude
```

#### Vector Transformation and Extraction

```cpp
Vector3F TransformVector(const Vector3F& v) const;  // Rotate a direction vector by this rotor
float Angle() const;                                // Rotation angle in radians (from scalar part)
Vector3F Axis() const;                              // Rotation axis (unit vector, from spatial part)
constexpr Rotor operator*(float s) const;           // Scalar multiplication
```

#### Namespace-Level Utilities

```cpp
Rotor Normalize(const Rotor& r);
bool operator==(const Rotor& a, const Rotor& b);
bool operator!=(const Rotor& a, const Rotor& b);
constexpr Rotor operator*(float s, const Rotor& r);
```

### Motor

A motor encodes both rotation and translation. It is the GA equivalent of a unit dual quaternion: `DQ = Qr + eps*Qd`.

| Field | Meaning |
|-------|---------|
| `w` | Real scalar (rotation quaternion w) |
| `spatial` | Real bivector - rotational part (e12, e23, e31) |
| `ew` | Dual scalar |
| `translational` | Dual bivector - translation-coupled part (e01, e02, e03) |

Translation extraction: `t = 2 * vec(Qd x conj(Qr))`. Construction from transform: `Qd = (t/2 as pure quat) x Qr`.

#### Construction

```cpp
constexpr Motor();                                                          // Identity motor {1, 0, 0, 0}
constexpr Motor(float w, const Vector3F& spatial, float ew, const Vector3F& translational);
static constexpr Motor Identity();                                          // Static identity factory
static Motor FromTransform(const Vector3F& position, const Quaternion& rotation);
static Motor FromTransform(const Vector3F& position, const Rotor& rotation);
```

#### Matrix Roundtrip

```cpp
Mat4 ToMat4() const;        // Extracts rotation quaternion and translation into Mat4
static Motor FromMat4(const Mat4& m);  // Extracts rotation from upper-left 3x3 and translation from column 3
```

#### Accessors

```cpp
constexpr Quaternion GetRotation() const;   // Extract real-part rotation as quaternion
Vector3F GetTranslation() const;            // Extract translation: t = 2 * vec(Qd x conj(Qr))
Rotor GetRotor() const;                     // Extract real part as Rotor {w, spatial}
```

#### Composition and Inversion

```cpp
Motor operator*(const Motor& o) const;   // Compose transforms via dual-quaternion multiplication with normalization
Motor Inverse() const;                   // Conjugate divided by |qr|^2 (~= conjugate for valid motor)
Motor Normalize() const;                 // Normalize real part, orthogonalize dual part
```

#### Transform Operations

```cpp
Vector3F TransformPoint(const Vector3F& p) const;     // Rotate and translate a point
Vector3F TransformDirection(const Vector3F& d) const; // Rotate a direction (no translation)
constexpr Motor operator*(float s) const;              // Scalar multiplication
```

#### Namespace-Level Utilities

```cpp
Motor Normalize(const Motor& m);
bool operator==(const Motor& a, const Motor& b);
bool operator!=(const Motor& a, const Motor& b);
constexpr Motor operator*(float s, const Motor& m);
```

### Rotor <-> Motor Conversion Helpers

```cpp
Motor FromRotor(const Rotor& r);   // Embed rotor as zero-translation motor
Rotor ToRotor(const Motor& m);     // Extract real-part rotor from motor
```

### Motor Slerp

Interpolates between two motors with spherical linear interpolation for rotation and linear blend for translation:

```cpp
Motor MotorSlerp(const Motor& a, const Motor& b, float t);
```

Ensures shortest-path rotation by negating `b`'s quaternion when `dot(qa, qb) < 0`.

### Plane

An oriented plane in 3D represented by a normal vector and signed distance from the origin. Plane equation: `n*x + d = 0`, where `|n| = 1` for a normalized plane.

#### Construction

```cpp
constexpr Plane();                                                        // Zero-initialized
constexpr Plane(const Vector3F& normal, float d);                         // From normal and distance
constexpr Plane(float nx, float ny, float nz, float pd);                  // Component constructor
static constexpr Plane XY();   // { (0, 0, 1), 0 } - X-Y plane at origin
static constexpr Plane YZ();   // { (1, 0, 0), 0 } - Y-Z plane at origin
static constexpr Plane ZX();   // { (0, 1, 0), 0 } - Z-X plane at origin
static Plane FromPointNormal(const Vector3F& point, const Vector3F& n);   // Normalized from point and normal
static Plane FromPoints(const Vector3F& p0, const Vector3F& p1, const Vector3F& p2);  // CCW winding -> outward normal
```

#### Distance and Classification

```cpp
constexpr float Distance(const Vector3F& p) const;       // Signed distance from point to plane
Vector3F ClosestPoint(const Vector3F& p) const;          // Closest point on plane to a given point
constexpr bool IsFrontFacing(const Vector3F& p) const;   // Distance(p) > 0
constexpr bool IsBackFacing(const Vector3F& p) const;    // Distance(p) < 0
```

#### Normalization and Orientation

```cpp
Plane Normalize() const;          // Normalize normal and scale distance accordingly
constexpr Plane Flipped() const;  // Negate both normal and distance
```

#### Transform by Motor, Rotor, Quaternion, or Matrix

```cpp
Plane Transform(const Motor& m) const;      // Rotate normal + translate offset point
Plane Transform(const Rotor& r) const;      // Pure rotation of normal and offset point
Plane Transform(const Quaternion& q) const; // Pure rotation via quaternion
Plane Transform(const Mat4& m) const;       // General transform using inverse-transpose of upper-left 3x3 for normal
```

#### Intersection

```cpp
// Plane-plane intersection: returns line direction and distances from origin
Vector3F Intersect(const Plane& other, float& outDistA, float& outDistB) const;

// Ray-plane intersection: returns true if ray intersects plane in forward direction
bool IntersectRay(const Vector3F& origin, const Vector3F& direction, float& outT) const;
```

#### Arithmetic and Equality

```cpp
constexpr Plane operator*(float s) const;
Plane operator*(float s, const Plane& p);   // Reverse scalar multiplication
bool operator==(const Plane& a, const Plane& b);
bool operator!=(const Plane& a, const Plane& b);
Plane Normalize(const Plane& p);             // Namespace-level normalize
```

---

## Usage Pattern

Include the umbrella header to access all math functionality:

```cpp
#include "Math/Math.h"
```

This transitively includes `Config.h`, `Types.h`, `Vector.h`, `Matrix.h`, `Quaternion.h`, and other sub-headers. For minimal builds, individual headers can be included directly.

### Example: Building a Transform

```cpp
using namespace Math;

Vector3F position = { 1.0f, 2.0f, 3.0f };
Vector3F rotation = { 0.0f, 45.0f, 0.0f };   // degrees
Vector3F scale    = { 1.0f, 1.0f, 1.0f };

Mat4 localTransform = Translate(position) * Mat4Cast(Quaternion(Radians(rotation))) * Scale(scale);
Mat4 worldTransform = parentGlobalTransform * localTransform;
```

### Example: Ray-Plane Intersection

```cpp
using namespace Math;

Ray ray { cameraPosition, Normalized(target - cameraPosition) };
float dist = Dot(planeNormal, ray.m_Direction);
Vector3F hitPoint = ray.m_Origin + ray.m_Direction * (Dot(planeNormal, planeCenter - ray.m_Origin) / dist);
```

---

## Design Notes

- **Left-handed coordinate system:** All functions produce left-handed results. Cross products, look-at matrices, and handedness-sensitive operations follow this convention.
- **Depth range `[0, 1]`:** Projection matrices use D3D-compatible depth. This matches the DirectX 12 backend.
- **Native types, no GLM dependency:** The library implements its own vector, matrix, and quaternion types rather than wrapping a third-party library. This avoids ABI mismatches with shader constant buffers and keeps SIMD optimizations intact.
- **SIMD with fallback:** SSE4.1 and AVX2 optimized routines are available when the corresponding macros are defined; all operations have scalar fallbacks for compatibility.
- **Namespace isolation:** Everything lives in `Math::` to avoid polluting the global namespace while keeping call sites concise (`Math::Vector3F`, `Math::Dot`, etc.).
