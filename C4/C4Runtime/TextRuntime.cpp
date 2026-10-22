// Number <-> text conversion behind the format_double / parse_double builtins (Bedrock's StringBuilder and the JSON /
// YAML readers and writers). The shortest form is Schubfach's digits laid out in SSE2 lanes (ShortestDouble.h), fixed
// decimals come from the C++ library's charconv, correctly rounded, so nothing depends on the C library's printf or
// locale. Ordinary decimals parse on the Eisel-Lemire fast path (DecimalToDouble.h); the rest through charconv, and
// strtod's own syntax through strtod.

#include "DecimalToDouble.h"
#include "ShortestDouble.h"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace
{
	// The C libraries disagree on how they spell NaN ("nan", "-nan(ind)"); C4 writes the same text everywhere.
	const char* NonFiniteText(double value)
	{
		if (std::isnan(value)) return "nan";
		if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
		return nullptr;
	}

	int32_t Copied(char* out, int32_t cap, const char* text, size_t length)
	{
		if (length >= (size_t)cap) return -1;
		std::memcpy(out, text, length);
		out[length] = 0;
		return (int32_t)length;
	}

	// Straight into `out` when it has room for any shortest text.
	int32_t FormatShortest(char* out, int32_t cap, double value)
	{
		if (cap > kShortestDoubleRoom)
		{
			const int length = WriteShortestDouble(out, value);
			out[length] = 0;
			return length;
		}
		char text[kShortestDoubleRoom];
		return Copied(out, cap, text, (size_t)WriteShortestDouble(text, value));
	}

	int32_t FormatFixed(char* out, int32_t cap, double value, int decimals)
	{
		const std::to_chars_result written = std::to_chars(out, out + cap - 1, value, std::chars_format::fixed, decimals);
		if (written.ec != std::errc()) return -1;
		*written.ptr = 0;
		return (int32_t)(written.ptr - out);
	}

	// What charconv does not read as strtod would: leading space, a `+`, and hexadecimal floats.
	bool NeedsStrtod(const char* text, int64_t length)
	{
		const char* at = text;
		const char* end = text + length;
		if (at < end && (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r' || *at == '\f' || *at == '\v' || *at == '+')) return true;
		if (at < end && *at == '-') ++at;
		return end - at >= 2 && at[0] == '0' && (at[1] == 'x' || at[1] == 'X');
	}

	// strtod needs a terminator, so the bytes are copied.
	int64_t ParseWithStrtod(const char* text, int64_t length, double* out)
	{
		char local[512];
		char* copy = length < (int64_t)sizeof(local) ? local : (char*)std::malloc((size_t)length + 1);
		if (!copy) return 0;
		std::memcpy(copy, text, (size_t)length);
		copy[length] = 0;
		char* end = nullptr;
		const double value = std::strtod(copy, &end);
		const int64_t taken = (int64_t)(end - copy);
		if (copy != local) std::free(copy);
		if (taken == 0) return 0;
		*out = value;
		return taken;
	}
}

// `value` as text into `out` (capacity `cap`, null-terminated): `decimals` fraction digits, or, with a negative
// `decimals`, the shortest text that reads back as the same double; NaN and the infinities are `nan`, `inf` and `-inf`.
// Returns the length, or -1 when `out` is too small.
extern "C" int32_t c4_format_double(char* out, int32_t cap, double value, int32_t decimals)
{
	if (!out || cap <= 0) return -1;
	if (const char* nonFinite = NonFiniteText(value)) return Copied(out, cap, nonFinite, std::strlen(nonFinite));
	if (decimals >= 0) return FormatFixed(out, cap, value, decimals > 100 ? 100 : decimals);
	return FormatShortest(out, cap, value);
}

// `value` as c4_format_double writes a double, the shortest text being the shortest that reads back as the same
// float (charconv's): 0.1f is `0.1`, not the double it widens to.
extern "C" int32_t c4_format_float(char* out, int32_t cap, float value, int32_t decimals)
{
	if (!out || cap <= 0) return -1;
	if (const char* nonFinite = NonFiniteText(value)) return Copied(out, cap, nonFinite, std::strlen(nonFinite));
	if (decimals >= 0) return FormatFixed(out, cap, value, decimals > 100 ? 100 : decimals);
	const std::to_chars_result written = std::to_chars(out, out + cap - 1, value);
	if (written.ec != std::errc()) return -1;
	*written.ptr = 0;
	return (int32_t)(written.ptr - out);
}

// The double that text[0, length) starts with, in strtod's syntax. Returns how many bytes it took, 0 when the text does
// not start with a number.
extern "C" int64_t c4_parse_double(const char* text, int64_t length, double* out)
{
	if (!text || !out || length <= 0) return 0;
	if (NeedsStrtod(text, length)) return ParseWithStrtod(text, length, out);
	if (const int64_t taken = ParseDecimalFast(text, length, out)) return taken;
	double value = 0;
	const std::from_chars_result read = std::from_chars(text, text + length, value);
	if (read.ec == std::errc::result_out_of_range) return ParseWithStrtod(text, length, out); // strtod's infinity or zero
	if (read.ec != std::errc()) return 0;
	*out = value;
	return (int64_t)(read.ptr - text);
}
