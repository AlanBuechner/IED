#ifndef STRUCTPTRINTEROP_H
#define STRUCTPTRINTEROP_H

// (a) value-only struct - like InteropSmoke's Point (known-good baseline).
typedef struct { int a; int b; } Simple;
int read_simple(const Simple* p);          // returns p->a
int read_simple_b(const Simple* p);        // returns p->b

// (b) struct WITH pointer fields - like WNDCLASSEX. first@0, ptrField@8, second@16.
typedef struct { int first; void* ptrField; int second; } WithPtr;
int read_withptr(const WithPtr* p);        // returns p->first
int read_withptr_second(const WithPtr* p); // returns p->second

#endif
