/* SIMD vector types as C spells them (clang's __m128, __m128i, __m256 and __m512 are these typedefs). */
typedef float Float4 __attribute__((__vector_size__(16), __aligned__(16)));
typedef long long Int64x2 __attribute__((__vector_size__(16), __aligned__(16)));
typedef float Float8 __attribute__((__vector_size__(32), __aligned__(32)));
typedef double Double4 __attribute__((__vector_size__(32), __aligned__(32)));
typedef float Float16 __attribute__((__vector_size__(64), __aligned__(64)));

Float4 Twice4(Float4 v);
Int64x2 AddPairs(Int64x2 a, Int64x2 b);
Float8 Scale8(Float8 v, float s);
Double4 Square4(Double4 v);
Float16 Negate16(Float16 v);
float SumThrough(const Float8* v);
