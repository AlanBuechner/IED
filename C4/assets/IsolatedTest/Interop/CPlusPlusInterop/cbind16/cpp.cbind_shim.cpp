#define _CRT_SECURE_NO_WARNINGS
#include <new>
#include "cpp.h"
// ctor wrapper for Counter
extern "C" void cbind_Counter_Construct_0(void* self) {
	new (self) Counter();
}

// ctor wrapper for Counter
extern "C" void cbind_Counter_Construct_1(void* self, int32_t initial) {
	new (self) Counter(initial);
}

// dtor wrapper for Counter
extern "C" void cbind_Counter_Finalize(void* self) {
	static_cast<Counter*>(self)->~Counter();
}

// method wrapper for Counter::Add
extern "C" void cbind_Counter_Add(void* self, int32_t amount) {
	return (*static_cast<Counter*>(self)).Add(amount);
}

// method wrapper for Counter::Get
extern "C" int32_t cbind_Counter_Get(void* self) {
	return (*static_cast<Counter*>(self)).Get();
}

// static wrapper for Counter::IncrementGlobal
extern "C" void cbind_Counter_st_IncrementGlobal() {
	return Counter::IncrementGlobal();
}

// static wrapper for Counter::Scale
extern "C" int32_t cbind_Counter_st_Scale(int32_t v, int32_t factor) {
	return Counter::Scale(v, factor);
}

// ctor wrapper for Bank
extern "C" void cbind_Bank_Construct(void* self) {
	new (self) Bank();
}

// dtor wrapper for Bank
extern "C" void cbind_Bank_Finalize(void* self) {
	static_cast<Bank*>(self)->~Bank();
}

// method wrapper for Bank::Deposit
extern "C" int32_t cbind_Bank_Deposit_1(void* self, int32_t amount) {
	return (*static_cast<Bank*>(self)).Deposit(amount);
}

// method wrapper for Bank::Deposit
extern "C" int32_t cbind_Bank_Deposit_2(void* self, int32_t amount, int32_t fee) {
	return (*static_cast<Bank*>(self)).Deposit(amount, fee);
}

// method wrapper for Bank::Balance
extern "C" int32_t cbind_Bank_Balance(void* self) {
	return (*static_cast<Bank*>(self)).Balance();
}

// ctor wrapper for Shape
extern "C" void cbind_Shape_Construct(void* self) {
	new (self) Shape();
}

// dtor wrapper for Shape
extern "C" void cbind_Shape_Finalize(void* self) {
	static_cast<Shape*>(self)->~Shape();
}

// method wrapper for Shape::Area [virtual]
extern "C" int32_t cbind_Shape_Area(void* self) {
	return (*static_cast<Shape*>(self)).Area();
}

// method wrapper for Shape::Id
extern "C" int32_t cbind_Shape_Id(void* self) {
	return (*static_cast<Shape*>(self)).Id();
}

// ctor wrapper for Circle
extern "C" void cbind_Circle_Construct(void* self, int32_t radius) {
	new (self) Circle(radius);
}

// dtor wrapper for Circle
extern "C" void cbind_Circle_Finalize(void* self) {
	static_cast<Circle*>(self)->~Circle();
}

// method wrapper for Circle::Area [virtual]
extern "C" int32_t cbind_Circle_Area(void* self) {
	return (*static_cast<Circle*>(self)).Area();
}

// free-function wrapper for AddVec
extern "C" Vec2 cbind_fn_AddVec(Vec2 a, Vec2 b) {
	return AddVec(a, b);
}

