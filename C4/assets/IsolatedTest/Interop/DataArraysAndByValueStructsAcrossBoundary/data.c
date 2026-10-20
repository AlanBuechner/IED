#include "data.h"
#include <string.h>
int sum_array(int* data, int n) { int s = 0; for (int i = 0; i < n; i++) s += data[i]; return s; }
Pt make_pt(int a, int b) { Pt p; p.x = a; p.y = b; return p; }
int pt_x(Pt p) { return p.x; }
int len_str(const char* s) { return (int)strlen(s); }
void fill_ints(int* out, int n) { for (int i = 0; i < n; i++) out[i] = i * i; }
