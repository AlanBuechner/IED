// Extension Method Tests - Header
// `this` is implicit - no explicit self parameter
// Operator overloads must be extension methods or struct body methods only.
// Standalone [operator("...")] functions without `extends` are INVALID and produce an error.

struct Point {
	float x;
	float y;
};

struct IPoint {
	int32 x;
	int32 y;
};

// Counter with create operator inside the struct body (constructor)
struct Counter {
	int32 value;
	
	[operator("create")]
	void Construct();
	
	[operator("create")]
	void Construct(int32 initial);
};

// --- Non-Operator Extension Methods ---

float Length() extends Point;
void SetXY(float nx, float ny) extends Point;
Point Normalized() extends Point;

int32 GetValue() extends Counter;
void IncrementValue() extends Counter;

// --- Arithmetic Operators as Extension Methods ---

[operator("add")]
Point Add(const Point& other) extends Point;

[operator("subtract")]
Point Subtract(const Point& other) extends Point;

[operator("multiply")]
Point Multiply(float s) extends Point;

[operator("divide")]
Point Divide(float s) extends Point;

// --- Comparison Operators as Extension Methods ---

[operator("equal")]
int32 Equals(const Point& other) extends Point;

[operator("notequal")]
int32 NotEquals(const Point& other) extends Point;

[operator("less")]
int32 LessThan(const Point& other) extends Point;

[operator("greater")]
int32 GreaterThan(const Point& other) extends Point;

// --- Unary Operators as Extension Methods ---

[operator("negate")]
Point Negate() extends Point;

[operator("unary_plus")]
Point UnaryPlus() extends Point;

// --- Assignment Operators as Extension Methods ---

[operator("assign")]
void Assign(const Point& other) extends Point;

[operator("add_assign")]
void AddAssign(const Point& other) extends Point;

[operator("subtract_assign")]
void SubtractAssign(const Point& other) extends Point;

// --- Bitwise Operators as Extension Methods on IPoint ---

[operator("bitwise_and")]
IPoint BitwiseAnd(const IPoint& other) extends IPoint;

[operator("bitwise_or")]
IPoint BitwiseOr(const IPoint& other) extends IPoint;

[operator("bitwise_xor")]
IPoint BitwiseXor(const IPoint& other) extends IPoint;

[operator("bitwise_not")]
IPoint BitwiseNot() extends IPoint;

// --- Shift Operators as Extension Methods ---

[operator("shift_left")]
IPoint ShiftLeft(int32 bits) extends IPoint;

[operator("shift_right")]
IPoint ShiftRight(int32 bits) extends IPoint;

// --- Increment/Decrement as Extension Methods ---

[operator("increment")]
void Increment() extends Point;

[operator("decrement")]
void Decrement() extends Point;

// --- Index Operators as Extension Methods ---

[operator("index")]
float GetIndex(int32 i) extends Point;

[operator("index_assign")]
void SetIndex(int32 i, float v) extends Point;

// --- Logical Operators as Extension Methods ---

[operator("logical_and")]
int32 LogicalAnd(const Point& other) extends Point;

[operator("logical_or")]
int32 LogicalOr(const Point& other) extends Point;

// --- Cast Operator as Extension Method ---

[operator("cast")]
float ToFloat() extends Point;

// --- Destructor and copy as extension methods ---

[operator("delete")]
void Destroy() extends Counter;

[operator("copy")]
void CopyFrom(const Counter& other) extends Counter;

// --- Test Functions ---

void TestExtensionMethodBasic();
void TestExtensionMethodOperators();
void TestExtensionMethodComparison();
void TestExtensionMethodAssignment();
void TestExtensionMethodBitwise();
void TestExtensionMethodShift();
void TestExtensionMethodIndex();
void TestExtensionMethodUnary();
void TestExtensionMethodLogical();
void TestExtensionMethodCast();
void TestExtensionMethodCreateDestroy();
void TestExtensionMethodCopyMove();
