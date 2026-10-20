#include "ns.h"

namespace mathlib {

int32_t Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

Matrix::Matrix() {}
Matrix::~Matrix() {}
int32_t Matrix::Trace() const { return 42; }
int32_t Matrix::IdentitySize() { return 3; }
int32_t Matrix::Rank() const { return 0; }

DiagMatrix::DiagMatrix(int32_t d) : d_(d) {}
int32_t DiagMatrix::Rank() const { return d_; }

namespace detail {
	int32_t DefaultMaxIter() { return 100; }
}

} // namespace mathlib
