// OrderIndependence - file-scope declaration ORDER must not matter (02-pipeline "Order-independence").
// Pass 1 registers every shell; pass 2 resolves layouts/constants to a fixed point, so NO forward
// declaration is needed outside function bodies. These cases write NO `class X;` forward declaration:
// each type is simply defined later in the unit and still usable from an earlier position.

// Case 1 (strongest): OI_A holds OI_B BY VALUE, but OI_B is defined LATER in this file. A's layout
// needs B's real size/offset, which only exists once pass 2 resolves B - no forward declaration written.
struct OI_A { public OI_B b; public int32 x; };
struct OI_B { public int32 y; };

// Case 2: a template instantiated with a type defined LATER in this file (OI_Used comes after OI_Wrap).
template<T> struct OI_Wrap { public T val; };
struct OI_Used { public int32 e; };

void TestOrderIndependence();
