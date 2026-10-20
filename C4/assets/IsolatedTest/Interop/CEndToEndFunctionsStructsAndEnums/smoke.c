#include "smoke.h"
int add(int a, int b) { return a + b; }
int do_thing(Point p, enum Color c) { return p.x * 2 + (int)c; }
float scale_point(const Point* p, float s) { return p->x * s; }
