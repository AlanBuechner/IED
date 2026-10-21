#pragma once
// Decimal text to a double without a fallback for every input a program writes: up to 19 significant digits, the exponent
// in range. Clinger's exact path first, then the Eisel-Lemire algorithm (Lemire, "Number Parsing at a Gigabyte per
// Second", 2021; Mushtak and Lemire, "Fast Number Parsing Without Fallback", 2023), correctly rounded like strtod.

#include <stdint.h>

// The double that text[0, length) starts with - an optional '-', digits with an optional '.', an optional exponent -
// into `out`, and how many bytes it took. 0 when the text is not such a number or needs the slow path: more than 19
// significant digits, an exponent out of range, "inf", "nan", a '+' or a hexadecimal float.
int64_t ParseDecimalFast(const char* text, int64_t length, double* out);
