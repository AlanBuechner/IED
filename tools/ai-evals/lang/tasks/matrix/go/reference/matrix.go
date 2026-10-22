package main

import (
	"math"
	"slices"
)

const smallestPivot = 1e-12

// Matrix is a dense matrix of float64 values.
type Matrix struct {
	rows, columns int
	values        []float64 // row by row
}

// NewMatrix returns a rows x columns matrix of zeros; a zero-size matrix is allowed.
func NewMatrix(rows, columns int) Matrix {
	return Matrix{rows: rows, columns: columns, values: make([]float64, rows*columns)}
}

func (m Matrix) Rows() int    { return m.rows }
func (m Matrix) Columns() int { return m.columns }

func (m Matrix) At(row, column int) float64 { return m.values[row*m.columns+column] }

func (m *Matrix) Set(row, column int, value float64) { m.values[row*m.columns+column] = value }

func (m Matrix) sameShape(other Matrix) bool {
	return m.rows == other.rows && m.columns == other.columns
}

func (m Matrix) clone() Matrix {
	return Matrix{rows: m.rows, columns: m.columns, values: slices.Clone(m.values)}
}

// Add works element by element; it returns a 0x0 matrix when the shapes differ.
func (m Matrix) Add(other Matrix) Matrix {
	if !m.sameShape(other) {
		return Matrix{}
	}
	sum := NewMatrix(m.rows, m.columns)
	for i := range sum.values {
		sum.values[i] = m.values[i] + other.values[i]
	}
	return sum
}

// Sub works element by element; it returns a 0x0 matrix when the shapes differ.
func (m Matrix) Sub(other Matrix) Matrix {
	if !m.sameShape(other) {
		return Matrix{}
	}
	difference := NewMatrix(m.rows, m.columns)
	for i := range difference.values {
		difference.values[i] = m.values[i] - other.values[i]
	}
	return difference
}

// Mul is the matrix product; it returns a 0x0 matrix when m's columns are not other's rows.
func (m Matrix) Mul(other Matrix) Matrix {
	if m.columns != other.rows {
		return Matrix{}
	}
	width := other.columns
	product := NewMatrix(m.rows, width)
	for i := 0; i < m.rows; i++ {
		out := product.values[i*width : (i+1)*width]
		for k := 0; k < m.columns; k++ {
			factor := m.values[i*m.columns+k]
			for j, value := range other.values[k*width : (k+1)*width] {
				out[j] += factor * value
			}
		}
	}
	return product
}

// Scale multiplies every element by factor.
func (m Matrix) Scale(factor float64) Matrix {
	scaled := NewMatrix(m.rows, m.columns)
	for i, value := range m.values {
		scaled.values[i] = value * factor
	}
	return scaled
}

// Equal reports whether the matrices have the same shape and every element equal.
func (m Matrix) Equal(other Matrix) bool {
	return m.sameShape(other) && slices.Equal(m.values, other.values)
}

func (m Matrix) Transposed() Matrix {
	transposed := NewMatrix(m.columns, m.rows)
	for row := 0; row < m.rows; row++ {
		for column := 0; column < m.columns; column++ {
			transposed.Set(column, row, m.At(row, column))
		}
	}
	return transposed
}

// Identity returns the size x size identity matrix.
func Identity(size int) Matrix {
	identity := NewMatrix(size, size)
	for i := 0; i < size; i++ {
		identity.Set(i, i, 1)
	}
	return identity
}

func (m *Matrix) swapRows(a, b int) {
	for column := 0; column < m.columns; column++ {
		ia, ib := a*m.columns+column, b*m.columns+column
		m.values[ia], m.values[ib] = m.values[ib], m.values[ia]
	}
}

// subtractRow subtracts factor times row source from row target.
func (m *Matrix) subtractRow(target, source int, factor float64) {
	for column := 0; column < m.columns; column++ {
		m.values[target*m.columns+column] -= factor * m.values[source*m.columns+column]
	}
}

func (m *Matrix) scaleRow(row int, factor float64) {
	for column := 0; column < m.columns; column++ {
		m.values[row*m.columns+column] *= factor
	}
}

// pivotRow is the row at or below column whose element in column is largest in absolute value.
func (m Matrix) pivotRow(column int) int {
	best := column
	for row := column + 1; row < m.rows; row++ {
		if math.Abs(m.At(row, column)) > math.Abs(m.At(best, column)) {
			best = row
		}
	}
	return best
}

// Inverse uses Gauss-Jordan elimination with partial pivoting. It returns a 0x0 matrix and false for a non-square
// matrix or a pivot whose absolute value is below 1e-12.
func (m Matrix) Inverse() (Matrix, bool) {
	if m.rows != m.columns {
		return Matrix{}, false
	}
	work := m.clone()
	result := Identity(m.rows)
	for column := 0; column < m.columns; column++ {
		pivot := work.pivotRow(column)
		value := work.At(pivot, column)
		if math.Abs(value) < smallestPivot {
			return Matrix{}, false
		}
		work.swapRows(pivot, column)
		result.swapRows(pivot, column)
		work.scaleRow(column, 1/value)
		result.scaleRow(column, 1/value)
		for row := 0; row < m.rows; row++ {
			factor := work.At(row, column)
			if row == column || factor == 0 {
				continue
			}
			work.subtractRow(row, column, factor)
			result.subtractRow(row, column, factor)
		}
	}
	return result, true
}

// Determinant is for square matrices only; it is 0 for a non-square one.
func (m Matrix) Determinant() float64 {
	if m.rows != m.columns {
		return 0
	}
	work := m.clone()
	determinant := 1.0
	for column := 0; column < m.columns; column++ {
		pivot := work.pivotRow(column)
		value := work.At(pivot, column)
		if value == 0 {
			return 0
		}
		if pivot != column {
			work.swapRows(pivot, column)
			determinant = -determinant
		}
		determinant *= value
		for row := column + 1; row < m.rows; row++ {
			work.subtractRow(row, column, work.At(row, column)/value)
		}
	}
	return determinant
}
