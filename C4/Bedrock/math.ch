// The math library: `include "math.ch"` (bedrock.ch does not include it, so a program that does not ask for
// it may name its own `Math`). Everything is in `namespace Math` (D-46): scalar functions and constants,
// TVector2/3/4<T> over packed SIMD lanes with swizzles, column-major Mat2/3/4, Quat, the left-handed,
// 0-to-1-depth transform and camera matrices, and batches of N vectors in structure-of-arrays form.

include "Math/scalar.ch"
include "Math/vector.ch"
include "Math/matrix.ch"
include "Math/quaternion.ch"
include "Math/transform.ch"
include "Math/batch.ch"
