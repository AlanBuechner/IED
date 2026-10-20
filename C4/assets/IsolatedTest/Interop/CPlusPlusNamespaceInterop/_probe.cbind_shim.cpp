#include <new>
#include "ns.h"
using namespace mathlib;
using namespace mathlib::detail;
// free-function wrapper for mathlib::Dot
extern "C" int32_t cbind_fn_Dot(Vec3 a, Vec3 b) {
	return mathlib::Dot(a, b);
}

// ctor wrapper for mathlib::Matrix
extern "C" void cbind_Matrix_Construct(void* self) {
	new (self) mathlib::Matrix();
}

// dtor wrapper for mathlib::Matrix
extern "C" void cbind_Matrix_Finalize(void* self) {
	static_cast<mathlib::Matrix*>(self)->~Matrix();
}

// method wrapper for mathlib::Matrix::Trace
extern "C" int32_t cbind_Matrix_Trace(void* self) {
	return (*static_cast<mathlib::Matrix*>(self)).Trace();
}

// static wrapper for mathlib::Matrix::IdentitySize
extern "C" int32_t cbind_Matrix_st_IdentitySize() {
	return mathlib::Matrix::IdentitySize();
}

// method wrapper for mathlib::Matrix::Rank [virtual]
extern "C" int32_t cbind_Matrix_Rank(void* self) {
	return (*static_cast<mathlib::Matrix*>(self)).Rank();
}

// ctor wrapper for mathlib::DiagMatrix
extern "C" void cbind_DiagMatrix_Construct(void* self, int32_t d) {
	new (self) mathlib::DiagMatrix(d);
}

// dtor wrapper for mathlib::DiagMatrix
extern "C" void cbind_DiagMatrix_Finalize(void* self) {
	static_cast<mathlib::DiagMatrix*>(self)->~DiagMatrix();
}

// method wrapper for mathlib::DiagMatrix::Rank [virtual]
extern "C" int32_t cbind_DiagMatrix_Rank(void* self) {
	return (*static_cast<mathlib::DiagMatrix*>(self)).Rank();
}

// free-function wrapper for mathlib::detail::DefaultMaxIter
extern "C" int32_t cbind_fn_DefaultMaxIter() {
	return mathlib::detail::DefaultMaxIter();
}

