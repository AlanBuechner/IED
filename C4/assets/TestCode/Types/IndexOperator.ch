// Types - Bracket index operator ([]) dispatch test declarations.
// [operator("[]")] / [operator("[]=")] are the symbol spellings of index / index_assign;
// obj[i] and obj[i] = v on a VALUE receiver must dispatch to them (pointer receivers keep pointer-indexing).

struct IdxBuf {
	public int32[4] data;
	[operator("[]")] public int32 Index(int32 i) { return data[i]; }
	[operator("[]=")] public void SetIndex(int32 i, int32 val) { data[i] = val; }
};

// The index operators live on the BASE: a derived object must find them through the hierarchy.
class IdxBase {
	public int32[4] data;
	[operator("[]")] public int32 Index(int32 i) { return data[i]; }
	[operator("[]=")] public void SetIndex(int32 i, int32 val) { data[i] = val; }
};
class IdxDerived : IdxBase {
	public int32 tag;
};

// The indexed member is NOT the first field: a store must go through the setter, never a raw GEP.
struct IdxOffset {
	int32 tag;
	int32[4] data;
	[operator("[]")] public int32 Index(int32 i) { return data[i]; }
	[operator("[]=")] public void SetIndex(int32 i, int32 val) { data[i] = val; }
};

void TestBracketIndexOperator();
void TestInheritedAndOffsetIndex();
