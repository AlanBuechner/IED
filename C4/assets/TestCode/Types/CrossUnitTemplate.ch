// Cross-unit template monomorphization regression (backlog bug #1).
// CURef<T> is instantiated by CUBox in THIS header's compilation unit (a non-entry unit), but HIR
// lowers only entry units. Before the fix, the instantiation's method signatures were recorded in
// this unit's per-unit maps and never reached the entry unit, so a reference-returning method like
// Deref lowered as a value (declared ptr, body ret i32) or emitted a void-typed badref load.
template<T>
struct CURef {
    public T* items;
    public uint32 index;
    [operator("dereference")] public T& Deref() { return this.items[this.index]; }
}

struct CUBox {
    public CURef<int32> r;
}

void TestCrossUnitTemplate();
