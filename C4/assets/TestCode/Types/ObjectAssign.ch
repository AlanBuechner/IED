// ObjectAssign - whole-object copy-assign where one side is reference-wrapped.
// Regression lock: the assignment-compatibility gate unwraps Reference/Alias from both sides to
// "judge compatibility against the value types", but only checked numeric/funptr/pointer/
// string-literal - never whether the UNWRAPPED value types are identical. So `S y; y = constRef;`
// (bare S lvalue <- const S&) was a false "Cannot assign value of type 'S' to 'S'". The same shape
// inside a template (`Vector<T>::Set`: data[i] is bare T, v is const T&) produced the Bedrock
// Vec3/Vec2/Pt2/VecHolder warnings. Now identical unwrapped value types are accepted.
struct OA { int32 a; float f; };

const OA& OA_Ref(const OA& v) { return v; }

template<T>
struct OABox {
    T* data;
    public void Put(uint32 i, const T& v) { data[i] = v; }
    public T& At(uint32 i) { return data[i]; }
};

void TestObjectAssignFromRef();
void TestObjectAssignTemplate();
