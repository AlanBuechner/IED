#ifndef PTRCASTEDGE_H
#define PTRCASTEDGE_H
// Foreign-C oracles: read back what a C4 int->pointer cast actually produced, so the test
// verifies the *real* 64-bit value foreign code sees (not just that C4's own deref works).
long long read_ptr(void* p);              // *p as a 64-bit integer
long long read_struct_field(const void* s); // s = struct{void* f}; return f
#endif
