// ns.h - C++ side exercising NAMESPACED interop (a very common real-world layout).
// Everything lives inside `mathlib` (and a nested `mathlib::detail`) so the binding generator
// must descend into namespaces, emit unique wrapper symbols, and produce a compiling shim.
#include <cstdint>

namespace mathlib {

enum Axis { X = 0, Y = 1, Z = 2 };

struct Vec3 {
	int32_t x, y, z;
};

// Free function in the namespace (mangled by the C++ compiler -> needs a wrapper).
int32_t Dot(Vec3 a, Vec3 b);

// Class with methods + a virtual, in the namespace.
class Matrix {
public:
	Matrix();
	~Matrix();
	int32_t Trace() const;
	static int32_t IdentitySize();
	virtual int32_t Rank() const;   // base returns 0
};

class DiagMatrix : public Matrix {
public:
	DiagMatrix(int32_t d);
	int32_t Rank() const override;  // returns d
private:
	int32_t d_;
};

namespace detail {
	// Nested namespace: a helper type + function.
	struct Config { int32_t maxIter; };
	int32_t DefaultMaxIter();
}

} // namespace mathlib
