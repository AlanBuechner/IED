// A COM-style interface: abstract, no destructor of its own (as WebView2's are), and one with a virtual destructor.
class IShape {
public:
	virtual int Area() = 0;
	virtual int Sides() = 0;
};

class IOwned {
public:
	virtual ~IOwned() {}
	virtual int Value() = 0;
};

IShape* MakeSquare(int side);
IOwned* MakeOwned(int value);
void FreeShape(IShape* shape);
