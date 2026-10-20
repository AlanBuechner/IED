#include "StructPtrInterop.h"
int read_simple(const Simple* p) { return p->a; }
int read_simple_b(const Simple* p) { return p->b; }
int read_withptr(const WithPtr* p) { return p->first; }
int read_withptr_second(const WithPtr* p) { return p->second; }
