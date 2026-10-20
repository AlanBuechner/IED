#pragma once
// Functions a header defines: static inline ones (no symbol of their own), a C99 inline one whose external
// definition is in the source, a void one, and a plain external one.
#include <stdint.h>

static inline int32_t Twice(int32_t x) { return x * 2; }
static inline void Store(int32_t* into, int32_t value) { *into = value; }
static inline double Half(double x) { return x / 2.0; }
inline int32_t Square(int32_t x) { return x * x; }
int32_t Add(int32_t a, int32_t b);
