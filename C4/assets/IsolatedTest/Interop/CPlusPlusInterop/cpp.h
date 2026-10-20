// cpp.h - C++ side of the CppInterop test. Exercises every binding category the
// generator must handle: data members, ctors (incl. implicit), methods with by-value
// struct params/returns, overloads, statics, virtual dispatch + inheritance, and a
// destructor that reports finalization.

#include <cstdint>

struct Vec2 {
	int32_t x;
	int32_t y;
};

class Counter {
public:
	Counter() : value(0) {}
	explicit Counter(int32_t initial) : value(initial) {}
	~Counter();

	void Add(int32_t amount);
	int32_t Get() const;
	static int32_t Scale(int32_t v, int32_t factor);
	static void IncrementGlobal();
	static int32_t g_global;

private:
	int32_t value;
};

// Overloaded method: two distinct signatures.
class Bank {
public:
	Bank() : balance(0) {}
	~Bank();

	int32_t Deposit(int32_t amount);            // overload 1 param
	int32_t Deposit(int32_t amount, int32_t fee); // overload 2 params
	int32_t Balance() const;

private:
	int32_t balance;
};

// Inheritance + virtual dispatch. C++ places the vptr at offset 0 of Shape (the key
// function class); Circle inherits it and adds radius after the base subobject.
class Shape {
public:
	Shape() : id(0) {}
	virtual ~Shape();

	virtual int32_t Area() const;   // base returns -1
	int32_t Id() const { return id; }

protected:
	int32_t id;
};

class Circle : public Shape {
public:
	Circle(int32_t radius) : radius(radius) { id = 7; }
	~Circle();

	int32_t Area() const override;   // returns radius * radius

private:
	int32_t radius;
};

// Free function with a by-value struct parameter and return (exercises the same ABI path).
Vec2 AddVec(Vec2 a, Vec2 b);

// Unsigned and character types keep their signedness and kind across the binding (BUG-149).
uint32_t BigUnsigned();
unsigned char HighByte();
signed char LowByte();
const wchar_t* WideHello();
int32_t WideLength(const wchar_t* text);
