package main

import (
	"math"
	"testing"
	"time"
)

func hiddenOf(rows, columns int, values ...float64) Matrix {
	m := NewMatrix(rows, columns)
	for i, value := range values {
		m.Set(i/columns, i%columns, value)
	}
	return m
}

func hiddenIs(m Matrix, rows, columns int, values ...float64) bool {
	if m.Rows() != rows || m.Columns() != columns {
		return false
	}
	for i, value := range values {
		if math.Abs(m.At(i/columns, i%columns)-value) > 1e-9 {
			return false
		}
	}
	return true
}

func hiddenIsEmpty(m Matrix) bool {
	return m.Rows() == 0 && m.Columns() == 0
}

func hiddenCheck(t *testing.T, ok bool, what string) {
	t.Helper()
	if !ok {
		t.Error(what)
	}
}

func hiddenNear(t *testing.T, what string, want, got, tolerance float64) {
	t.Helper()
	if math.Abs(want-got) > tolerance {
		t.Errorf("%s: got %v, want %v", what, got, want)
	}
}

func TestHiddenConstructionAndShape(t *testing.T) {
	m := NewMatrix(2, 3)
	hiddenCheck(t, m.Rows() == 2, "rows")
	hiddenCheck(t, m.Columns() == 3, "columns")
	hiddenCheck(t, hiddenIs(m, 2, 3, 0, 0, 0, 0, 0, 0), "all zero")
	hiddenCheck(t, hiddenIsEmpty(NewMatrix(0, 0)), "a 0x0 matrix")
}

func TestHiddenGetSet(t *testing.T) {
	m := NewMatrix(3, 2)
	m.Set(0, 1, 2.5)
	m.Set(2, 0, -4)
	m.Set(0, 1, 7.25)
	hiddenNear(t, "overwritten", 7.25, m.At(0, 1), 0)
	hiddenNear(t, "last row", -4, m.At(2, 0), 0)
	hiddenCheck(t, hiddenIs(m, 3, 2, 0, 7.25, 0, 0, -4, 0), "the others stay zero")
}

func TestHiddenAddSubtract(t *testing.T) {
	a := hiddenOf(2, 3, 1, 2, 3, 4, 5, 6)
	b := hiddenOf(2, 3, 0.5, -1, 10, 4, 0, -6)
	hiddenCheck(t, hiddenIs(a.Add(b), 2, 3, 1.5, 1, 13, 8, 5, 0), "sum")
	hiddenCheck(t, hiddenIs(a.Sub(b), 2, 3, 0.5, 3, -7, 0, 5, 12), "difference")
	hiddenCheck(t, hiddenIs(a, 2, 3, 1, 2, 3, 4, 5, 6), "the left operand is unchanged")
	hiddenCheck(t, hiddenIs(b, 2, 3, 0.5, -1, 10, 4, 0, -6), "the right operand is unchanged")
}

func TestHiddenMultiply(t *testing.T) {
	a := hiddenOf(2, 3, 1, 2, 3, 4, 5, 6)
	b := hiddenOf(3, 2, 7, 8, 9, 10, 11, 12)
	hiddenCheck(t, hiddenIs(a.Mul(b), 2, 2, 58, 64, 139, 154), "2x3 times 3x2")
	hiddenCheck(t, hiddenIs(b.Mul(a), 3, 3, 39, 54, 69, 49, 68, 87, 59, 82, 105), "3x2 times 2x3")
}

func TestHiddenScale(t *testing.T) {
	a := hiddenOf(2, 3, 1, -2, 3, 0, 5.5, -6)
	hiddenCheck(t, hiddenIs(a.Scale(2), 2, 3, 2, -4, 6, 0, 11, -12), "times two")
	hiddenCheck(t, hiddenIs(a.Scale(-0.5), 2, 3, -0.5, 1, -1.5, 0, -2.75, 3), "times minus a half")
}

func TestHiddenEquality(t *testing.T) {
	a := hiddenOf(2, 2, 1, 2, 3, 4)
	hiddenCheck(t, a.Equal(hiddenOf(2, 2, 1, 2, 3, 4)), "the same values")
	hiddenCheck(t, !a.Equal(hiddenOf(2, 2, 1, 2, 3, 4.5)), "one value differs")
	wide := NewMatrix(2, 3)
	hiddenCheck(t, !wide.Equal(NewMatrix(3, 2)), "the shapes differ")
	empty := NewMatrix(0, 0)
	hiddenCheck(t, empty.Equal(NewMatrix(0, 0)), "two empty matrices")
}

func TestHiddenTransposed(t *testing.T) {
	a := hiddenOf(2, 3, 1, 2, 3, 4, 5, 6)
	transposed := a.Transposed()
	hiddenCheck(t, hiddenIs(transposed, 3, 2, 1, 4, 2, 5, 3, 6), "transposed")
	twice := transposed.Transposed()
	hiddenCheck(t, twice.Equal(a), "transposed twice")
}

func TestHiddenIdentity(t *testing.T) {
	identity := Identity(3)
	hiddenCheck(t, hiddenIs(identity, 3, 3, 1, 0, 0, 0, 1, 0, 0, 0, 1), "identity")
	a := hiddenOf(3, 2, 1, 2, 3, 4, 5, 6)
	left, right := identity.Mul(a), a.Mul(Identity(2))
	hiddenCheck(t, left.Equal(a), "identity times a")
	hiddenCheck(t, right.Equal(a), "a times identity")
}

func TestHiddenInverse3x3(t *testing.T) {
	a := hiddenOf(3, 3, 0, 1, 2, 1, 0, 3, 4, -3, 8)
	inverse, ok := a.Inverse()
	hiddenCheck(t, ok, "invertible")
	hiddenNear(t, "its determinant", -2, a.Determinant(), 1e-9)
	hiddenCheck(t, hiddenIs(a, 3, 3, 0, 1, 2, 1, 0, 3, 4, -3, 8), "the matrix is unchanged")
	if inverse.Rows() != 3 || inverse.Columns() != 3 {
		t.Fatalf("the inverse is %dx%d", inverse.Rows(), inverse.Columns())
	}
	for i, want := range []float64{-4.5, 7, -1.5, -2, 4, -1, 1.5, -2, 0.5} {
		hiddenNear(t, "an element of the inverse", want, inverse.At(i/3, i%3), 1e-9)
	}
	hiddenCheck(t, hiddenIs(a.Mul(inverse), 3, 3, 1, 0, 0, 0, 1, 0, 0, 0, 1), "a times its inverse")
}

func TestHiddenInverseRefused(t *testing.T) {
	singular := hiddenOf(3, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9)
	inverse, ok := singular.Inverse()
	hiddenCheck(t, !ok && hiddenIsEmpty(inverse), "singular")
	tiny := hiddenOf(2, 2, 1, 1, 1, 1+1e-14)
	inverse, ok = tiny.Inverse()
	hiddenCheck(t, !ok && hiddenIsEmpty(inverse), "a pivot below 1e-12")
	wide := hiddenOf(2, 3, 1, 0, 0, 0, 1, 0)
	inverse, ok = wide.Inverse()
	hiddenCheck(t, !ok && hiddenIsEmpty(inverse), "not square")
}

func TestHiddenDeterminant(t *testing.T) {
	cases := []struct {
		what string
		m    Matrix
		want float64
	}{
		{"2x2", hiddenOf(2, 2, 3, 8, 4, 6), -14},
		{"3x3", hiddenOf(3, 3, 6, 1, 1, 4, -2, 5, 2, 8, 7), -306},
		{"a row swap", hiddenOf(2, 2, 0, 1, 1, 0), -1},
		{"4x4", hiddenOf(4, 4, 2, 0, 1, 3, 1, 1, 0, 2, 0, 3, 1, 1, 4, 1, 2, 0), -32},
		{"singular", hiddenOf(3, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9), 0},
		{"identity", Identity(5), 1},
		{"not square", hiddenOf(2, 3, 1, 2, 3, 4, 5, 6), 0},
	}
	for _, c := range cases {
		hiddenNear(t, c.what, c.want, c.m.Determinant(), 1e-9)
	}
}

func TestHiddenMismatchedShapes(t *testing.T) {
	a := hiddenOf(2, 3, 1, 2, 3, 4, 5, 6)
	hiddenCheck(t, hiddenIsEmpty(a.Add(NewMatrix(3, 2))), "sum")
	hiddenCheck(t, hiddenIsEmpty(a.Sub(NewMatrix(2, 2))), "difference")
	hiddenCheck(t, hiddenIsEmpty(a.Mul(a)), "product")
}

func TestHiddenLargeProduct(t *testing.T) {
	const size = 300
	a, b := NewMatrix(size, size), NewMatrix(size, size)
	for i := 0; i < size; i++ {
		for j := 0; j < size; j++ {
			a.Set(i, j, float64((i*7+j*3)%11))
			b.Set(i, j, float64((i*5+j*2)%13))
		}
	}
	started := time.Now()
	product := a.Mul(b)
	if elapsed := time.Since(started); elapsed > 5*time.Second {
		t.Errorf("a 300x300 product took %v", elapsed)
	}
	if product.Rows() != size || product.Columns() != size {
		t.Fatalf("the product is %dx%d", product.Rows(), product.Columns())
	}
	hiddenNear(t, "(0, 0)", 8990, product.At(0, 0), 1e-9)
	hiddenNear(t, "(17, 123)", 9044, product.At(17, 123), 1e-9)
	hiddenNear(t, "(150, 7)", 8935, product.At(150, 7), 1e-9)
	hiddenNear(t, "(299, 299)", 8982, product.At(299, 299), 1e-9)
}
