#pragma once
#include <stdint.h>

// One struct per Windows x64 by-value size class so we can exercise every passing convention:
// 1/2/4/8 bytes go in a single integer register; any other size goes by hidden pointer (param)
// and sret out-pointer (return). C4 must match clang exactly for each.
typedef struct { int8_t   a; }                S1;   // 1 byte  -> i8
typedef struct { int16_t  a; }                S2;   // 2 bytes -> i16
typedef struct { int8_t   a, b, c; }          S3;   // 3 bytes -> ptr/sret
typedef struct { int32_t  a; }                S4;   // 4 bytes -> i32
typedef struct { int8_t   a, b, c, d, e; }    S5;   // 5 bytes -> ptr/sret
typedef struct { int16_t  a, b, c; }          S6;   // 6 bytes -> ptr/sret
typedef struct { int8_t   a, b, c, d, e, f, g;} S7; // 7 bytes -> ptr/sret
typedef struct { int64_t  a; }                S8;   // 8 bytes -> i64
typedef struct { int32_t  a, b, c; }          S12;  // 12 bytes-> ptr/sret
typedef struct { int32_t  a, b, c, d; }       S16;  // 16 bytes-> ptr/sret
typedef struct { int32_t  a, b, c, d, e; }    S20;  // 20 bytes-> ptr/sret

// Floating-point aggregates. Windows x64 passes these in the SAME integer-register classes as the
// integer structs of equal size (verified against clang: float->{i32}, double/{float,float}/
// {int32_t,float} -> {i64}); only a pure packed SIMD vector keeps its <N x T> XMM type. C4's
// GetByValueIntType bitcasts any power-of-2 aggregate <= 8 bytes to iN, so these must round-trip
// with the exact float/double values intact through both param and sret directions.
typedef struct { float    a; }                F1;   // 4 bytes -> i32 (single float)
typedef struct { double   a; }                D1;   // 8 bytes -> i64 (single double)
typedef struct { float    a, b; }             F2;   // 8 bytes -> i64 (two floats)
typedef struct { int32_t  a; float b; }       M1;   // 8 bytes -> i64 (mixed int+float, not homogeneous)
typedef struct { float    a, b, c, d; }       F4;   // 16 bytes-> ptr/sret (four floats)

// Param passing: sum members (exercises the incoming by-value argument).
float SumF1(F1 v);   double SumD1(D1 v);   float SumF2(F2 v);   int32_t SumM1(M1 v);   float SumF4(F4 v);

// Return passing: build a value from n and return it by value (sret for F4).
F1  MakeF1(int32_t n);   D1  MakeD1(int32_t n);   F2  MakeF2(int32_t n);   M1  MakeM1(int32_t n);   F4  MakeF4(int32_t n);

// Param passing: sum all members (exercises the incoming by-value argument).
int32_t SumS1(S1 v);   int32_t SumS2(S2 v);   int32_t SumS3(S3 v);
int32_t SumS4(S4 v);   int32_t SumS5(S5 v);   int32_t SumS6(S6 v);
int32_t SumS7(S7 v);   int32_t SumS8(S8 v);
int32_t SumS12(S12 v); int32_t SumS16(S16 v); int32_t SumS20(S20 v);

// Return passing (sret): build a value from n and return it by value.
S1  MakeS1(int32_t n);   S2  MakeS2(int32_t n);   S3  MakeS3(int32_t n);
S4  MakeS4(int32_t n);   S5  MakeS5(int32_t n);   S6  MakeS6(int32_t n);
S7  MakeS7(int32_t n);   S8  MakeS8(int32_t n);
S12 MakeS12(int32_t n);  S16 MakeS16(int32_t n);  S20 MakeS20(int32_t n);
