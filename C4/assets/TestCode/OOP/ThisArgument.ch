// OOP - `*this` passed as an argument is the object, `this` the pointer (BUG-157)
struct TaHolder {
	public int32 value;
	public int64 pad;
	public int32 ViaConst() const;
	public int32 ViaMutable();
	public int32 ViaLambda();
	public bool PointerIsThis();
}

void TestThisArgument();
