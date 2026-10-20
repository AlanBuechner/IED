// TemplateCompleteness - locks the catalog [OK] template cases (09-complex-cases.md) end to end:
// functions in templated classes, out-of-line method definitions on templated classes, scoped aliases
// in templates, non-type parameters, and sizeof of an instantiation in a const context. Each must work
// as a byproduct of the architecture - no special handling. The [BUG] catalog rows (nested template
// types through an instantiation, separated decl/def of function templates) are NOT tested here; they
// flip to passing once the rework lands and become tests at that point.

// Out-of-line method on a templated class: declared in the class, DEFINED after its first use.
template<T> struct TC_Vec { public T x; float Len(); };

// Inline function in a templated class (function bodies inside template definitions).
template<T> struct TC_Box { public T v; T Get() { return v; } T Add(T o) { return v + o; } };

// Scoped alias in a template: the alias names the template parameter and is used as a field type.
template<T> struct TC_Typed { alias Type = T; Type val; };

// Non-type parameter: the value binds at instantiation and sizes an array.
template<int N> struct TC_Arr { public int32[N] x; public int32 Count() { return N; } };

void TestTemplateCompleteness();
