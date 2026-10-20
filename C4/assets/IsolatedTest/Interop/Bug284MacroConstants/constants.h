#pragma once
// Object-like macros of every kind a constant can be, and some that are not constants.
#define SMALL 42
#define NEGATIVE (-7)
#define HEX_FLAGS 0x00000200
#define UNSIGNED_MAX 0xFFFFFFFFu
#define BIG 9000000000LL
#define BIG_UNSIGNED 18446744073709551615ULL
#define MOST_NEGATIVE (-2147483647 - 1)
#define COMBINED (HEX_FLAGS | SMALL)
#define CAST_LONG ((long)SMALL * 2)
#define CHARACTER 'A'
#define ENABLED 1
#define RATIO 0.125
#define SCALE 1.5f
#define LARGEST_FLOAT 3.40282347e+38F
#define GREETING "hello \"c4\"\n"
#define EMPTY
#define NOT_A_CONSTANT {0}
#define TWICE(x) ((x) * 2)
#define NAMED_LIKE_A_FUNCTION 5
int NAMED_LIKE_A_FUNCTION_REAL(void);
