#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int64_t native_add(int64_t a, int64_t b);
const char* native_greet(const char* name);
#ifdef __cplusplus
}
#endif