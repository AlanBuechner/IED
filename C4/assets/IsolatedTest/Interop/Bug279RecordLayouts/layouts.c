#include "layouts.h"
#include <stddef.h>

int64_t CLayout(int32_t index)
{
	const int64_t layout[] = {
		sizeof(U), offsetof(U, d),
		sizeof(struct HasU), offsetof(struct HasU, after),
		sizeof(struct Small1st), offsetof(struct Small1st, after),
		sizeof(struct Ovl), offsetof(struct Ovl, hEvent),
		sizeof(LI), offsetof(LI, QuadPart),
		sizeof(struct InAddr), sizeof(struct SockAddrIn), offsetof(struct SockAddrIn, zero),
		sizeof(struct Outer), offsetof(struct Outer, z),
		sizeof(struct Flex),
		sizeof(struct Wide), offsetof(struct Wide, after),
		sizeof(struct Bits), offsetof(struct Bits, d), offsetof(struct Bits, g),
		sizeof(struct Packed), offsetof(struct Packed, c),
		sizeof(struct AnonStruct), offsetof(struct AnonStruct, z),
	};
	return index >= 0 && index < (int32_t)(sizeof(layout) / sizeof(layout[0])) ? layout[index] : -1;
}

void FillLI(LI* value) { value->QuadPart = 0x1122334455667788LL; }

void FillBits(struct Bits* value)
{
	value->a = 5;
	value->b = 17;
	value->c = 1000;
	value->d = 77;
	value->e = 3;
	value->f = 9;
	value->g = 500;
}

uint32_t ReadOvlOffset(const struct Ovl* value) { return value->Offset; }

uint8_t ReadSecondByte(const struct SockAddrIn* value) { return value->addr.S_un.S_un_b.b2; }
