package main

// ApplyUnary applies prefix - (to an int) or ! (to anything) to a value.
func ApplyUnary(op TokenKind, operand Value, line int) (Value, error) {
	if op == TokenBang {
		return BoolValue(!IsTruthy(operand)), nil
	}
	number, ok := operand.(IntValue)
	if !ok {
		return nil, errorAt(line, "cannot apply '%s' to %s", Spelling(op), operand.TypeName())
	}
	return -number, nil
}

// ApplyBinary applies an arithmetic, comparison or equality operator; + also joins two strings.
func ApplyBinary(op TokenKind, left, right Value, line int) (Value, error) {
	switch op {
	case TokenEqual:
		return BoolValue(ValuesEqual(left, right)), nil
	case TokenNotEqual:
		return BoolValue(!ValuesEqual(left, right)), nil
	}
	if joined, ok := joinStrings(op, left, right); ok {
		return joined, nil
	}
	a, leftIsInt := left.(IntValue)
	b, rightIsInt := right.(IntValue)
	if !leftIsInt || !rightIsInt {
		return nil, errorAt(line, "cannot apply '%s' to %s and %s", Spelling(op), left.TypeName(), right.TypeName())
	}
	return applyIntegers(op, a, b, line)
}

func joinStrings(op TokenKind, left, right Value) (Value, bool) {
	a, leftIsString := left.(StringValue)
	b, rightIsString := right.(StringValue)
	if op != TokenPlus || !leftIsString || !rightIsString {
		return nil, false
	}
	return a + b, true
}

func applyIntegers(op TokenKind, a, b IntValue, line int) (Value, error) {
	switch op {
	case TokenPlus:
		return a + b, nil
	case TokenMinus:
		return a - b, nil
	case TokenStar:
		return a * b, nil
	case TokenSlash, TokenPercent:
		return divide(op, a, b, line)
	}
	return compareIntegers(op, a, b), nil
}

// divide is / or %, both truncating toward zero.
func divide(op TokenKind, a, b IntValue, line int) (Value, error) {
	if b == 0 {
		return nil, errorAt(line, "division by zero")
	}
	if op == TokenSlash {
		return a / b, nil
	}
	return a % b, nil
}

func compareIntegers(op TokenKind, a, b IntValue) Value {
	switch op {
	case TokenLess:
		return BoolValue(a < b)
	case TokenLessEqual:
		return BoolValue(a <= b)
	case TokenGreater:
		return BoolValue(a > b)
	}
	return BoolValue(a >= b)
}
