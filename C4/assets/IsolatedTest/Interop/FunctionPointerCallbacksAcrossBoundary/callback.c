#include "callback.h"
int apply_op(OpFn fn, int x, int y) { return fn(x, y); }
void for_each(int n, void (*cb)(int i)) { for (int i = 0; i < n; i++) cb(i); }
