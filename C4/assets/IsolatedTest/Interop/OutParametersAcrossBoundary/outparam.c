#include "outparam.h"
#include <stdlib.h>
#include <stdio.h>
int make_box(Box** ppBox, int value) {
    if (!ppBox) return 1;
    Box* b = (Box*)calloc(1, sizeof(Box));
    if (!b) return 2;
    b->x = value;
    *ppBox = b;
    return 0;
}
char* make_message(int code) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "code=%d", code);
    return buf;
}
