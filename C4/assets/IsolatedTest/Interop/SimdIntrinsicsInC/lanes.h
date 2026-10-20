/* What lanes.c computes with AVX2 and FMA intrinsics; the header spells __m256 / __m256i as their typedefs. */
typedef float Lanes8 __attribute__((__vector_size__(32), __aligned__(32)));
typedef long long Words4 __attribute__((__vector_size__(32), __aligned__(32)));

Lanes8 FusedStep(Lanes8 position, Lanes8 velocity, float dt);
Words4 MaskOfNegatives(Lanes8 v);
int CountNegatives(Lanes8 v);
