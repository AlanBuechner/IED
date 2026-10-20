#include <new>
#include "ref.h"
// ctor wrapper for RefOps
extern "C" void cbind_RefOps_Construct(void* self) {
	new (self) RefOps();
}

// dtor wrapper for RefOps
extern "C" void cbind_RefOps_Finalize(void* self) {
	static_cast<RefOps*>(self)->~RefOps();
}

// method wrapper for RefOps::Sum
extern "C" int32_t cbind_RefOps_Sum(void* self, const Pt & p) {
	return (*static_cast<RefOps*>(self)).Sum(p);
}

// method wrapper for RefOps::Scale
extern "C" void cbind_RefOps_Scale(void* self, Pt & p, int32_t k) {
	return (*static_cast<RefOps*>(self)).Scale(p, k);
}

