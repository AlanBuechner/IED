#include "vectors.h"

Float4 Twice4(Float4 v) { return v + v; }
Int64x2 AddPairs(Int64x2 a, Int64x2 b) { return a + b; }
Float8 Scale8(Float8 v, float s) { return v * s; }
Double4 Square4(Double4 v) { return v * v; }
Float16 Negate16(Float16 v) { return -v; }
float SumThrough(const Float8* v) { float s = 0.0f; for (int i = 0; i < 8; ++i) s += (*v)[i]; return s; }
