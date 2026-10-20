// Number <-> text conversion behind the format_double / parse_double builtins (Bedrock's StringBuilder
// and the JSON / YAML readers), built on the C library so rounding, infinities and NaN follow it.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The C libraries disagree on how they spell NaN ("nan", "-nan(ind)"); C4 writes the same text everywhere.
static const char* NonFiniteText(double value)
{
	if (isnan(value)) return "nan";
	if (isinf(value)) return value < 0 ? "-inf" : "inf";
	return NULL;
}

// The fewest significant digits (1 to 17) that read back as `value`, and the decimal exponent of its first one.
static int ShortestDigits(double value, int* exponent)
{
	char scientific[40];
	int digits = 1;
	for (; digits < 17; ++digits)
	{
		snprintf(scientific, sizeof(scientific), "%.*e", digits - 1, value);
		if (strtod(scientific, NULL) == value) break;
	}
	snprintf(scientific, sizeof(scientific), "%.*e", digits - 1, value);
	const char* e = strchr(scientific, 'e');
	*exponent = e ? atoi(e + 1) : 0;
	return digits;
}

// The shortest text that reads back as `value`: plain decimals while the exponent is from -5 to 15 (`1000`,
// `0.001`), scientific otherwise (`1e+16`, `1e-05`) - the choice Python's repr makes.
static int FormatShortest(char* out, int32_t cap, double value)
{
	int exponent = 0;
	int digits = ShortestDigits(value, &exponent);
	if (exponent < -5 || exponent > 15) return snprintf(out, (size_t)cap, "%.*e", digits - 1, value);
	int fraction = digits - 1 - exponent;
	return snprintf(out, (size_t)cap, "%.*f", fraction > 0 ? fraction : 0, value);
}

// `value` as text into `out` (capacity `cap`, null-terminated): `decimals` fraction digits, or, with a
// negative `decimals`, the shortest text that reads back as the same double; NaN and the infinities are
// `nan`, `inf` and `-inf`. Returns the length, or -1 when `out` is too small.
int32_t c4_format_double(char* out, int32_t cap, double value, int32_t decimals)
{
	if (!out || cap <= 0) return -1;
	int n = -1;
	const char* nonFinite = NonFiniteText(value);
	if (nonFinite)
		n = snprintf(out, (size_t)cap, "%s", nonFinite);
	else if (decimals >= 0)
		n = snprintf(out, (size_t)cap, "%.*f", decimals > 100 ? 100 : decimals, value);
	else
		n = FormatShortest(out, cap, value);
	return (n < 0 || n >= cap) ? -1 : (int32_t)n;
}

// The double that text[0, length) starts with, in strtod's syntax. Returns how many bytes it took, 0 when
// the text does not start with a number. strtod needs a terminator, so the bytes are copied: pass the
// number's own bytes, not the rest of a document.
int64_t c4_parse_double(const char* text, int64_t length, double* out)
{
	char local[512];
	if (!text || !out || length <= 0) return 0;
	char* copy = length < (int64_t)sizeof(local) ? local : (char*)malloc((size_t)length + 1);
	if (!copy) return 0;
	memcpy(copy, text, (size_t)length);
	copy[length] = 0;
	char* end = NULL;
	double value = strtod(copy, &end);
	int64_t taken = (int64_t)(end - copy);
	if (copy != local) free(copy);
	if (taken == 0) return 0;
	*out = value;
	return taken;
}
