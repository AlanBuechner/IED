include "bedrock.ch"

namespace Linear
{
	class Matrix
	{
		// A rows x columns matrix of zeros; a zero-size matrix (0x0) is allowed.
		[operator("create")] public void Init(int64 rows, int64 columns);
		public int64 Rows() const;
		public int64 Columns() const;
		public double Get(int64 row, int64 column) const;
		public void Set(int64 row, int64 column, double value);
		// Element by element; a 0x0 matrix when the shapes differ.
		[operator("+")] public Matrix Add(const Matrix& other) const;
		[operator("-")] public Matrix Subtract(const Matrix& other) const;
		// The matrix product; a 0x0 matrix when this matrix's column count is not the other's row count.
		[operator("*")] public Matrix Multiply(const Matrix& other) const;
		// Every element times `factor`.
		[operator("*")] public Matrix Scale(double factor) const;
		// True when both have the same shape and every element is equal.
		[operator("==")] public bool Equals(const Matrix& other) const;
		public Matrix Transposed() const;
		// The size x size identity matrix.
		public static Matrix Identity(int64 size);
		// Gauss-Jordan elimination with partial pivoting. Returns false, leaving `inverse` untouched, for a
		// non-square matrix or when a pivot's absolute value is below 1e-12.
		public bool TryInverse(Matrix& inverse) const;
		// Square matrices only; 0 for a non-square matrix.
		public double Determinant() const;

		private int64 rows;
		private int64 columns;
		private Core::Array<double> values;   // row by row

		private bool SameShape(const Matrix& other) const;
		private void SwapRows(int64 a, int64 b);
		// Subtracts `factor` times row `source` from row `target`.
		private void SubtractRow(int64 target, int64 source, double factor);
		private void ScaleRow(int64 row, double factor);
		// The row at or below `column` whose element in `column` is largest in absolute value.
		private int64 PivotRow(int64 column) const;
	}
}
