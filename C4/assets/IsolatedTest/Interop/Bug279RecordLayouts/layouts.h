#pragma once
// Records whose C layout CBind must reproduce byte for byte: named and anonymous unions, fields of anonymous and
// nested record types, a flexible array, a type C4 has no spelling for, bitfields and a packed record.
#include <stdint.h>

typedef union U { int32_t i; double d; char c[12]; } U;
struct HasU { int32_t tag; union U u; int32_t after; };
struct Small1st { int32_t a; union { char c; double d; }; int32_t after; };
struct Ovl { uint64_t Internal; uint64_t InternalHigh; union { struct { uint32_t Offset; uint32_t OffsetHigh; }; void* Pointer; }; void* hEvent; };
typedef union LI { struct { uint32_t LowPart; int32_t HighPart; }; struct { uint32_t LowPart; int32_t HighPart; } u; int64_t QuadPart; } LI;
struct InAddr { union { struct { uint8_t b1, b2, b3, b4; } S_un_b; struct { uint16_t w1, w2; } S_un_w; uint32_t S_addr; } S_un; };
struct SockAddrIn { int16_t family; uint16_t port; struct InAddr addr; char zero[8]; };
struct Outer { int32_t x; struct Inner { int64_t y; } inner; int32_t z; };
struct Flex { int32_t n; int32_t items[]; };
struct Wide { int32_t a; __int128 big; int32_t after; };
struct Bits { uint32_t a : 4; uint32_t b : 12; uint32_t c : 16; uint32_t d; uint8_t e : 3; uint8_t f : 5; uint16_t g; };
#pragma pack(push, 1)
struct Packed { uint8_t a; uint32_t b; uint16_t c; };
#pragma pack(pop)
struct AnonStruct { int32_t a; struct { int32_t x; int32_t y; }; int32_t z; };

// The layout as C sees it: sizes and offsets in the order main.cf lists them.
int64_t CLayout(int32_t index);
void FillLI(LI* value);
void FillBits(struct Bits* value);
uint32_t ReadOvlOffset(const struct Ovl* value);
uint8_t ReadSecondByte(const struct SockAddrIn* value);
