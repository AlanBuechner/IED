#include "PtrCastEdge.h"
typedef struct { void* f; } Holder;
long long read_ptr(void* p) { return (long long)p; }
long long read_struct_field(const void* s) { return (long long)((const Holder*)s)->f; }
