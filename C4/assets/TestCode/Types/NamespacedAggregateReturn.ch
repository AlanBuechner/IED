// NamespacedAggregateReturn - returning a struct BY VALUE whose type name is namespace-qualified.
// Regression lock for the "value doesn't match function result type 'i64'" codegen crash: HIR's
// braced-init lowering resolved the aggregate's type by stripping to the last name segment and doing
// a flat symbol lookup, which failed for namespaced types (the stripped name isn't in global scope).
// The value was silently dropped, leaving the function body with no return terminator. Now the type
// is resolved through the single query facade (ResolveTypeByName), which handles namespaces, aliases,
// and template instances. All four shapes must build AND run:
//   a) global struct + global fn          (control - always worked)
//   b) namespaced struct + namespaced fn  (the reported crash)
//   c) global struct + namespaced fn
//   d) namespaced struct + global fn      (qualified name at global scope)

namespace NS {
    struct P2 { public float x; public float y; }
    public static P2 MakeP(float a, float b) { return P2{a, b}; }
}

struct GA { public float x; public float y; }
public static GA GMake(float a, float b) { return GA{a, b}; }

namespace NS2 {
    // (c): namespaced fn returning a GLOBAL struct.
    public static GA GInNS(float a, float b) { return GA{a, b}; }
}

// (d): global fn returning a NAMESPACE-QUALIFIED struct by value.
public static NS::P2 MakePFromGlobal(float a, float b) { return NS::P2{a, b}; }

void TestNamespacedAggregateReturn();
