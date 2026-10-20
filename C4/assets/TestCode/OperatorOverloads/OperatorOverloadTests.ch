// Operator Overloads - Comprehensive test declarations
// Tests ALL operator annotations as extension methods with `extends <typename>` syntax
// `this` is implicit - no explicit self parameter
// Standalone [operator("...")] functions without `extends` are INVALID and produce errors

struct OpVec2 {
	float x;
	float y;
};

struct OpIVec2 {
	int32 x;
	int32 y;
};

struct OpCounter {
	int32 value;

	[operator("create")]
	void Construct();

	[operator("create")]
	void Construct(int32 initial);
};

// ============================================
// OpVec2 Arithmetic Operators (extension methods)
// ============================================

[operator("add")]
OpVec2 Add(const OpVec2& other) extends OpVec2;

[operator("subtract")]
OpVec2 Subtract(const OpVec2& other) extends OpVec2;

[operator("multiply")]
OpVec2 Multiply(float s) extends OpVec2;

[operator("divide")]
OpVec2 Divide(float s) extends OpVec2;

// ============================================
// OpIVec2 Arithmetic Operators (extension methods)
// ============================================

[operator("add")]
OpIVec2 Add(const OpIVec2& other) extends OpIVec2;

[operator("subtract")]
OpIVec2 Subtract(const OpIVec2& other) extends OpIVec2;

[operator("modulo")]
OpIVec2 Modulo(const OpIVec2& other) extends OpIVec2;

// ============================================
// OpVec2 Comparison Operators (extension methods)
// ============================================

[operator("equal")]
int32 Equals(const OpVec2& other) extends OpVec2;

[operator("notequal")]
int32 NotEquals(const OpVec2& other) extends OpVec2;

[operator("less")]
int32 LessThan(const OpVec2& other) extends OpVec2;

[operator("greater")]
int32 GreaterThan(const OpVec2& other) extends OpVec2;

[operator("less_equal")]
int32 LessEqual(const OpVec2& other) extends OpVec2;

[operator("greater_equal")]
int32 GreaterEqual(const OpVec2& other) extends OpVec2;

// ============================================
// OpVec2 Unary Operators (extension methods)
// ============================================

[operator("negate")]
OpVec2 Negate() extends OpVec2;

[operator("unary_plus")]
OpVec2 UnaryPlus() extends OpVec2;

[operator("logical_not")]
int32 LogicalNot() extends OpVec2;

// ============================================
// OpIVec2 Bitwise Operators (extension methods)
// ============================================

[operator("bitwise_and")]
OpIVec2 BitwiseAnd(const OpIVec2& other) extends OpIVec2;

[operator("bitwise_or")]
OpIVec2 BitwiseOr(const OpIVec2& other) extends OpIVec2;

[operator("bitwise_xor")]
OpIVec2 BitwiseXor(const OpIVec2& other) extends OpIVec2;

[operator("bitwise_not")]
OpIVec2 BitwiseNot() extends OpIVec2;

// ============================================
// OpIVec2 Shift Operators (extension methods)
// ============================================

[operator("shift_left")]
OpIVec2 ShiftLeft(int32 bits) extends OpIVec2;

[operator("shift_right")]
OpIVec2 ShiftRight(int32 bits) extends OpIVec2;

// ============================================
// OpVec2 Assignment Operators (extension methods)
// ============================================

[operator("assign")]
void Assign(const OpVec2& other) extends OpVec2;

// ============================================
// OpVec2 Compound Assignment Operators (extension methods)
// ============================================

[operator("add_assign")]
void AddAssign(const OpVec2& other) extends OpVec2;

[operator("subtract_assign")]
void SubtractAssign(const OpVec2& other) extends OpVec2;

[operator("multiply_assign")]
void MultiplyAssign(float s) extends OpVec2;

[operator("divide_assign")]
void DivideAssign(float s) extends OpVec2;

// ============================================
// OpIVec2 Compound Assignment Operators (extension methods)
// ============================================

[operator("modulo_assign")]
void ModuloAssign(const OpIVec2& other) extends OpIVec2;

[operator("and_assign")]
void AndAssign(const OpIVec2& other) extends OpIVec2;

[operator("or_assign")]
void OrAssign(const OpIVec2& other) extends OpIVec2;

[operator("xor_assign")]
void XorAssign(const OpIVec2& other) extends OpIVec2;

// ============================================
// OpVec2 Logical Operators (extension methods)
// ============================================

[operator("logical_and")]
int32 LogicalAnd(const OpVec2& other) extends OpVec2;

[operator("logical_or")]
int32 LogicalOr(const OpVec2& other) extends OpVec2;

// ============================================
// Cast Operators (extension methods)
// ============================================

[operator("cast")]
float ToFloat() extends OpVec2;

[operator("cast")]
OpIVec2 ToIVec2() extends float;

// ============================================
// OpCounter Destroy (extension method)
// ============================================

[operator("delete")]
void Destroy() extends OpCounter;

// ============================================
// OpCounter Copy/Move (extension methods)
// ============================================

[operator("copy")]
void CopyFrom(const OpCounter& other) extends OpCounter;

// ============================================
// OpVec2 Increment/Decrement (extension methods)
// ============================================

[operator("increment")]
void Increment() extends OpVec2;

[operator("decrement")]
void Decrement() extends OpVec2;

// ============================================
// OpVec2 Index/Subscript Operators (extension methods)
// ============================================

[operator("index")]
float GetIndex(int32 i) extends OpVec2;

[operator("index_assign")]
void SetIndex(int32 i, float v) extends OpVec2;

// ============================================
// Non-Operator Extension Methods
// ============================================

float Length() extends OpVec2;

// ============================================
// Compound-assignment resolution: in-place <op>_assign vs plain fallback.
// These types isolate each form so the shared OperatorDispatch::ResolveCompound rule is exercised
// directly - a type may define ONLY the in-place form (no plain operator) or ONLY the plain form,
// and both must dispatch for `v op= rhs` / `v.field op= rhs`. Semantic and HIR resolve through the
// same brick, so they cannot disagree about whether the compound assignment is supported.
// ============================================

struct OpAssignOnly {
	int32 n;
};
[operator("multiply_assign")]
void AssignOnlyMul(int32 s) extends OpAssignOnly;   // in-place only: no plain [operator("multiply")]

struct OpPlainOnly {
	int32 n;
};
[operator("multiply")]
OpPlainOnly PlainOnlyMul(int32 s) extends OpPlainOnly;   // plain only: no <op>_assign form

struct OpFieldOuter {
	OpPlainOnly inner;   // object-typed field whose type has ONLY the plain operator
};

// ============================================
// Test Functions
// ============================================

void TestArithmeticOperators();
void TestComparisonOperators();
void TestUnaryOperators();
void TestBitwiseOperators();
void TestShiftOperators();
void TestAssignmentOperators();
void TestCompoundAssignmentOperators();
void TestLogicalOperators();
void TestCastOperator();
void TestCreateDestroyOperators();
void TestCopyMoveOperators();
void TestIncrementDecrementOperators();
void TestIndexOperator();
void TestSyntacticArithmeticDispatch();
void TestSyntacticComparisonDispatch();
void TestSyntacticBitwiseDispatch();
void TestSyntacticLogicalDispatch();
void TestCompoundAssignmentResolution();