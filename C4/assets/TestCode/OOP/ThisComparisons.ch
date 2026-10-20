// OOP - `this` in an expression is the receiver's address (BUG-170) and `*this` is the object, even when
// the type has its own dereference operator (BUG-171)
struct TcNode {
	public int32 value;
	public bool IsSame(const TcNode& other) const;
	public bool IsOther(const TcNode* other) const;
}

struct TcCursor {
	public int32* items;
	public int32 index;
	[operator("dereference")] public int32 Read() const { return items[index]; }
	public TcCursor Next() const;
	public int32 CopyThenRead() const;
}

void TestThisComparisons();
