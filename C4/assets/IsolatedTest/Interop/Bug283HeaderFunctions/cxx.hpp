#pragma once
// C++ functions a header defines: inline and static, and one in a namespace.
#include <cstdint>

inline int32_t Triple(int32_t x) { return x * 3; }
static int32_t Negate(int32_t x) { return -x; }
namespace math { inline int32_t Cube(int32_t x) { return x * x * x; } }
