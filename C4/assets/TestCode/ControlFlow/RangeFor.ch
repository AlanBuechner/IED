// ControlFlow - Range-For Loop Test declarations
void TestRangeForInts();
void TestRangeForFloats();
void TestRangeForStructs();
void TestRangeForBreak();
void TestRangeForContinue();
void TestRangeForIterator();
void TestRangeForTemplateContainer();
void TestRangeForByValueCall();

// User-defined iterator protocol types (declared before their extension methods).
int32[8] g_rfi_data;

struct RFICollection {
	int32 count;
};

struct RFIIterator {
	int32 index;
};

[operator("begin")] RFIIterator Begin() extends RFICollection {
	RFIIterator it;
	it.index = 0;
	return it;
}

[operator("end")] RFIIterator End() extends RFICollection {
	RFIIterator it;
	it.index = this.count;
	return it;
}

[operator("notequal")] int32 NotEqual(const RFIIterator& other) extends RFIIterator {
	return (this.index != other.index) ? 1 : 0;
}

[operator("dereference")] int32 Dereference() extends RFIIterator {
	return g_rfi_data[this.index];
}

[operator("increment")] RFIIterator Increment() extends RFIIterator {
	RFIIterator it;
	it.index = this.index + 1;
	return it;
}
