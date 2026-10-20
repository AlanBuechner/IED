// Types - nested braces build the elements of an __init_list<T> (BUG-177)
struct NilPair { public int32 a; public int32 b; }

class NilBag {
	public int32 total;
	public int32 count;
	[operator("create")] public void Init(__init_list<NilPair> pairs);
}

void TestNestedInitList();
