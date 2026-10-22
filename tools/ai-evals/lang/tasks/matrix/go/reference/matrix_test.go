package main

import (
	"math"
	"testing"
)

func square2(a, b, c, d float64) Matrix {
	m := NewMatrix(2, 2)
	m.Set(0, 0, a)
	m.Set(0, 1, b)
	m.Set(1, 0, c)
	m.Set(1, 1, d)
	return m
}

func TestMultiplyTwoByTwo(t *testing.T) {
	product := square2(1, 2, 3, 4).Mul(square2(5, 6, 7, 8))
	if !product.Equal(square2(19, 22, 43, 50)) {
		t.Fatalf("got %v", product)
	}
}

func TestInverseUndoesTheMatrix(t *testing.T) {
	m := square2(4, 7, 2, 6)
	inverse, ok := m.Inverse()
	if !ok {
		t.Fatal("not invertible")
	}
	back := m.Mul(inverse)
	for row := 0; row < 2; row++ {
		for column := 0; column < 2; column++ {
			want := 0.0
			if row == column {
				want = 1
			}
			if math.Abs(back.At(row, column)-want) > 1e-12 {
				t.Fatalf("m times its inverse is %v", back)
			}
		}
	}
	if math.Abs(m.Determinant()-10) > 1e-12 {
		t.Fatalf("determinant %v", m.Determinant())
	}
}

func TestZeroSize(t *testing.T) {
	empty := NewMatrix(0, 0)
	if empty.Rows() != 0 || !empty.Equal(NewMatrix(0, 0).Add(NewMatrix(2, 2))) {
		t.Fatal("a mismatch is not 0x0")
	}
}
