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
class Bank {
public:
	Bank() : balance(0) {}
	~Bank();
	int32_t Deposit(int32_t amount);
	int32_t Deposit(int32_t amount, int32_t fee);
	int32_t Balance() const;
private:
	int32_t balance;
};
class Shape {
public:
	Shape() : id(0) {}
	virtual ~Shape();
	virtual int32_t Area() const;
	int32_t Id() const { return id; }
protected:
	int32_t id;
};
class Circle : public Shape {
public:
	Circle(int32_t radius) : radius(radius) { id = 7; }
	~Circle();
	int32_t Area() const override;
private:
	int32_t radius;
};
Vec2 AddVec(Vec2 a, Vec2 b);
