#include "ref.h"

RefOps::RefOps() {}
RefOps::~RefOps() {}
int32_t RefOps::Sum(const Pt& p) { return p.x + p.y; }
void RefOps::Scale(Pt& p, int32_t k) { p.x *= k; p.y *= k; }
