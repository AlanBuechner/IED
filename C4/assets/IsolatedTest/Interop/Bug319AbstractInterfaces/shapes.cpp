#include "shapes.h"

namespace {
	class Square : public IShape {
	public:
		explicit Square(int side) : m_Side(side) {}
		int Area() override { return m_Side * m_Side; }
		int Sides() override { return 4; }
		int m_Side;
	};

	class Owned : public IOwned {
	public:
		explicit Owned(int value) : m_Value(value) {}
		int Value() override { return m_Value; }
		int m_Value;
	};
}

IShape* MakeSquare(int side) { return new Square(side); }
IOwned* MakeOwned(int value) { return new Owned(value); }
void FreeShape(IShape* shape) { delete static_cast<Square*>(shape); }
