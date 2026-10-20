// smoke.c - C implementation backing the InteropSmoke test. Compiled by c4.exe's
// interop pre-pass (extern "c" source) and linked into the final binary.
#include "smoke.h"

int add(int a, int b) { return a + b; }

int do_thing(Point p, enum Color c)
{
	return p.x * 2 + (int)c;
}

float scale_point(const Point* p, float s) { return p->x * s; }
