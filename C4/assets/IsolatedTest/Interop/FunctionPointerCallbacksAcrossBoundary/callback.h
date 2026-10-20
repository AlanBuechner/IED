typedef int (*OpFn)(int a, int b);
int apply_op(OpFn fn, int x, int y);
void for_each(int n, void (*cb)(int i));
