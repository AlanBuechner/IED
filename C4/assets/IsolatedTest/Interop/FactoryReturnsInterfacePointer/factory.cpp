#include "factory.h"
#include <new>
IShape::~IShape() {}
Circle::Circle(int32_t r) : radius(r) {}
Circle::~Circle() {}
int32_t Circle::Area() const { return radius * radius; }
IShape* CreateShape(int32_t kind, int32_t radius) {
    (void)kind;
    return new (std::nothrow) Circle(radius);
}
void ReleaseShape(IShape* s) { delete s; }
