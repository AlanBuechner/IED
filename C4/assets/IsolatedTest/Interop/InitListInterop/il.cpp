#include "il.h"
int32_t SumInts(std::initializer_list<int32_t> list) { int32_t s = 0; for (auto v : list) s += v; return s; }
float SumFloats(std::initializer_list<float> list) { float s = 0.0f; for (auto v : list) s += v; return s; }
double ProdDoubles(std::initializer_list<double> list) { double p = 1.0; for (auto v : list) p *= v; return p; }
int32_t CountChars(const std::initializer_list<char>& list) { int32_t n = 0; for (auto v : list) (void)v, ++n; return n; }
int32_t MinIntsRef(const std::initializer_list<int32_t>& list) { if (list.empty()) return 0; int32_t m = *list.begin(); for (auto v : list) if (v < m) m = v; return m; }
