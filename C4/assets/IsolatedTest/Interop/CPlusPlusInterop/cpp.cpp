// cpp.cpp - definitions for the CppInterop test. Compiled by the interop pass as a
// foreign object (clang -std=c++20, MSVC target) and linked into the C4 executable.

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

uint32_t BigUnsigned() { return 4000000000u; }
unsigned char HighByte() { return 200; }
signed char LowByte() { return -100; }
const wchar_t* WideHello() { return L"Hi"; }
int32_t WideLength(const wchar_t* text) { int32_t n = 0; while (text[n]) ++n; return n; }
