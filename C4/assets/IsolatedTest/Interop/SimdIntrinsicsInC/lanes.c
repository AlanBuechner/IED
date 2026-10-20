#include <immintrin.h>
#include "lanes.h"

Lanes8 FusedStep(Lanes8 position, Lanes8 velocity, float dt) { return (Lanes8)_mm256_fmadd_ps((__m256)velocity, _mm256_set1_ps(dt), (__m256)position); }
Words4 MaskOfNegatives(Lanes8 v) { return (Words4)_mm256_castps_si256(_mm256_cmp_ps((__m256)v, _mm256_setzero_ps(), _CMP_LT_OQ)); }
int CountNegatives(Lanes8 v) { return __builtin_popcount(_mm256_movemask_ps((__m256)v)); }
