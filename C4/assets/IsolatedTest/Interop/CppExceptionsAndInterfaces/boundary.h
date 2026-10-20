// boundary.h - C++ side of CppExceptionsAndInterfaces: exceptions crossing the boundary in both directions,
// and C++ calling methods of objects that C4 implements through interfaces.
#pragma once
#include <cstdint>

struct CppError { int32_t code; };
struct Handle { int64_t value; };

// C++ throws; C4 catches.
int32_t ThrowInt(int32_t v);
int32_t ThrowCppError(int32_t code);
int32_t ThrowErrorPointer(int32_t code);

// C4 throws from a callback; C++ catches (int, CppError, or anything else with catch (...)).
int32_t CatchFromCallback(int32_t (*callback)(int32_t), int32_t arg);

// C++ calls through C4-implemented interfaces laid out like these C++ ones:
//   struct IProvider { virtual Handle Get(int32_t base) = 0; virtual int32_t Count() = 0; };
//   struct ICounter  { virtual int32_t Next() = 0; };
int32_t UseProvider(void* provider);
int32_t UseCounter(void* counter, int32_t times);
