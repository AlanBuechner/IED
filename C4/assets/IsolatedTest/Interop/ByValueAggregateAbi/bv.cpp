#include "bv.h"

int32_t SumS1(S1 v)   { return v.a; }
int32_t SumS2(S2 v)   { return v.a; }
int32_t SumS3(S3 v)   { return v.a + v.b + v.c; }
int32_t SumS4(S4 v)   { return v.a; }
int32_t SumS5(S5 v)   { return v.a + v.b + v.c + v.d + v.e; }
int32_t SumS6(S6 v)   { return v.a + v.b + v.c; }
int32_t SumS7(S7 v)   { return v.a + v.b + v.c + v.d + v.e + v.f + v.g; }
int32_t SumS8(S8 v)   { return (int32_t)v.a; }
int32_t SumS12(S12 v){ return v.a + v.b + v.c; }
int32_t SumS16(S16 v){ return v.a + v.b + v.c + v.d; }
int32_t SumS20(S20 v){ return v.a + v.b + v.c + v.d + v.e; }

S1  MakeS1(int32_t n)   { S1 r;  r.a = (int8_t)n;              return r; }
S2  MakeS2(int32_t n)   { S2 r;  r.a = (int16_t)n;             return r; }
S3  MakeS3(int32_t n)   { S3 r;  r.a=(int8_t)n; r.b=(int8_t)(n+1); r.c=(int8_t)(n+2); return r; }
S4  MakeS4(int32_t n)   { S4 r;  r.a = n;                     return r; }
S5  MakeS5(int32_t n)   { S5 r;  r.a=(int8_t)n; r.b=(int8_t)(n+1); r.c=(int8_t)(n+2); r.d=(int8_t)(n+3); r.e=(int8_t)(n+4); return r; }
S6  MakeS6(int32_t n)   { S6 r;  r.a=(int16_t)n; r.b=(int16_t)(2*n); r.c=(int16_t)(3*n); return r; }
S7  MakeS7(int32_t n)   { S7 r;  r.a=(int8_t)n; r.b=(int8_t)(n+1); r.c=(int8_t)(n+2); r.d=(int8_t)(n+3); r.e=(int8_t)(n+4); r.f=(int8_t)(n+5); r.g=(int8_t)(n+6); return r; }
S8  MakeS8(int32_t n)   { S8 r;  r.a = (int64_t)n;             return r; }
S12 MakeS12(int32_t n)  { S12 r; r.a=n; r.b=2*n; r.c=3*n;      return r; }
S16 MakeS16(int32_t n)  { S16 r; r.a=n; r.b=2*n; r.c=3*n; r.d=4*n; return r; }
S20 MakeS20(int32_t n)  { S20 r; r.a=n; r.b=2*n; r.c=3*n; r.d=4*n; r.e=5*n; return r; }

float SumF1(F1 v)   { return v.a; }
double SumD1(D1 v)  { return v.a; }
float SumF2(F2 v)   { return v.a + v.b; }
int32_t SumM1(M1 v) { return v.a; }
float SumF4(F4 v)   { return v.a + v.b + v.c + v.d; }

F1  MakeF1(int32_t n)  { F1 r; r.a = (float)(n * 0.5f);  return r; }
D1  MakeD1(int32_t n)  { D1 r; r.a = (double)(n * 0.25); return r; }
F2  MakeF2(int32_t n)  { F2 r; r.a=(float)n; r.b=(float)(2*n); return r; }
M1  MakeM1(int32_t n)  { M1 r; r.a=n; r.b=(float)(n+1); return r; }
F4  MakeF4(int32_t n)  { F4 r; r.a=(float)n; r.b=(float)(2*n); r.c=(float)(3*n); r.d=(float)(4*n); return r; }
