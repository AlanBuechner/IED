#ifndef SMOKE_H
#define SMOKE_H
typedef struct { int x; float y; } Point;
enum Color { RED = 1, GREEN = 2, BLUE = 4 };
int add(int a, int b);
int do_thing(Point p, enum Color c);
float scale_point(const Point* p, float s);
#endif
