#include <initializer_list>
#include <cstdint>
// by-value initializer_list params (passed by reference across the FFI to match MSVC ABI)
int32_t SumInts(std::initializer_list<int32_t> list);
float SumFloats(std::initializer_list<float> list);
double ProdDoubles(std::initializer_list<double> list);
// const-reference initializer_list param (maps to __init_list<T>* in C4)
int32_t CountChars(const std::initializer_list<char>& list);
int32_t MinIntsRef(const std::initializer_list<int32_t>& list);
