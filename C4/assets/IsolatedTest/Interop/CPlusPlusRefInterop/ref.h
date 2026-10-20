// ref.h - C++ side exercising pass-by-reference (a very common C++ idiom).
#include <cstdint>

struct Pt { int32_t x; int32_t y; };

class RefOps {
public:
	RefOps();
	~RefOps();
	// const-ref param: read-only, no copy.  C4 binding exposes this as a Pt* - callers pass &x.
	int32_t Sum(const Pt& p);
	// non-const (in-out) ref param: modifies the caller's value.
	void Scale(Pt& p, int32_t k);
};
