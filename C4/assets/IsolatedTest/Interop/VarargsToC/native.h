#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t c_sum(int32_t count, ...);
double c_average(int32_t count, ...);
int64_t c_calls_back(void* summer);
#ifdef __cplusplus
}
#endif
