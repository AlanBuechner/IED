#include "cpp.h"
int32_t Counter::g_global = 100;
Counter::~Counter() {}
void Counter::Add(int32_t amount) { value += amount; }
int32_t Counter::Get() const { return value; }
int32_t Counter::Scale(int32_t v, int32_t factor) { return v * factor; }
void Counter::IncrementGlobal() { g_global++; }
Bank::~Bank() {}
int32_t Bank::Deposit(int32_t amount) { balance += amount; return balance; }
int32_t Bank::Deposit(int32_t amount, int32_t fee) { balance += amount - fee; return balance; }
int32_t Bank::Balance() const { return balance; }
Shape::~Shape() {}
int32_t Shape::Area() const { return -1; }
Circle::~Circle() {}
int32_t Circle::Area() const { return radius * radius; }
Vec2 AddVec(Vec2 a, Vec2 b) { return Vec2{ a.x + b.x, a.y + b.y }; }
