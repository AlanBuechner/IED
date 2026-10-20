// MethodCallOnExpressionReceiver tests - header.
// A method call whose receiver is a COMPLEX EXPRESSION (a container accessor) that returns
// T& or T* must bind to the object's real method definition. The container is a TEMPLATE and the
// call is nested as an argument to another function - the exact shape Bedrock's GC-container tests
// use (`assert_true(v.At(0)->Equals(Str::FromCStr("keep1")))`). This guards the O8 pointer-kind
// collapse: when `*` became a single Pointer kind, codegen's receiver naming had two drifted copies
// (primary path + fallback walk) and stopped dereferencing a pointer/reference-typed receiver, so
// the call fell to a broken variadic stub ("argument is not of expected type 'i32'").

// Note: this helper type is named TagVal (NOT "Str") - a second global type named Str would
// collide with Bedrock's class Str and its Equals method mangles to the same Str_Equals symbol,
// so only one body survives in the merged module and the other silently gets wrong code.

struct TagVal {
	int32 n;
	public int32 Equals(TagVal* other) { return (other != null && this.n == other->n) ? 1 : 0; }
};

template<T>
struct Box {
	T[2] data;
	T& GetAt(uint32 i);
	T* GetPtr(uint32 i);
};

void TestMethodCallOnExpressionReceiver();
