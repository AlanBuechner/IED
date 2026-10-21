// Number <-> text conversion behind the format_double / parse_double builtins (Bedrock's StringBuilder and the JSON /
// YAML readers and writers). The digits come from the C++ library's charconv: correctly rounded, and the shortest form
// is Ryu's, so neither depends on the C library's printf or locale. Ordinary decimals parse on the Eisel-Lemire fast
// path (DecimalToDouble.h); the rest through charconv, and strtod's own syntax through strtod.

#include "DecimalToDouble.h"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace
{
	// The exponents a shortest number is written plainly between (`1000`, `0.001`); outside them it is scientific
	// (`1e+16`, `1e-05`) - the choice Python's repr makes.
	constexpr int kLowestPlainExponent = -5;
	constexpr int kHighestPlainExponent = 15;
	// The longest scientific form of a double: a sign, 17 digits, a point, `e`, a sign and three exponent digits.
	constexpr int kScientificCapacity = 32;

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

	// The shortest digits that read back as a double, as its scientific form gives them.
	struct ShortestDigits
	{
		bool negative = false;
		char digits[17] = {};
		int count = 0;
		int exponent = 0; // of the first digit
	};

	// The exponent after a scientific form's `e`: a sign, then digits up to `end` (charconv writes no terminator).
	int ExponentOf(const char* at, const char* end)
	{
		const bool negative = at < end && *at == '-';
		if (at < end && (*at == '-' || *at == '+')) ++at;
		int exponent = 0;
		for (; at < end; ++at) exponent = exponent * 10 + (*at - '0');
		return negative ? -exponent : exponent;
	}

	ShortestDigits Decompose(const char* scientific, const char* end)
	{
		ShortestDigits shortest;
		const char* at = scientific;
		if (*at == '-') { shortest.negative = true; ++at; }
		for (; at < end && *at != 'e'; ++at)
			if (*at != '.') shortest.digits[shortest.count++] = *at;
		if (at < end) shortest.exponent = ExponentOf(at + 1, end);
		return shortest;
	}

	// `shortest` without an exponent: its digits around the point, zeros filling out to it on either side.
	int32_t WritePlain(char* out, int32_t cap, const ShortestDigits& shortest)
	{
		char text[kHighestPlainExponent - kLowestPlainExponent + kScientificCapacity];
		int length = 0;
		if (shortest.negative) text[length++] = '-';
		const int integerDigits = shortest.exponent + 1;
		if (integerDigits <= 0)
		{
			text[length++] = '0';
			text[length++] = '.';
			for (int zero = 0; zero < -integerDigits; ++zero) text[length++] = '0';
			for (int i = 0; i < shortest.count; ++i) text[length++] = shortest.digits[i];
			return Copied(out, cap, text, (size_t)length);
		}
		for (int i = 0; i < integerDigits; ++i) text[length++] = i < shortest.count ? shortest.digits[i] : '0';
		if (shortest.count > integerDigits)
		{
			text[length++] = '.';
			for (int i = integerDigits; i < shortest.count; ++i) text[length++] = shortest.digits[i];
		}
		return Copied(out, cap, text, (size_t)length);
	}

	// Between these magnitudes the shortest digits always have an exponent that is written plainly, so charconv's
	// shortest fixed form is the text, in one pass.
	constexpr double kSmallestPlain = 1e-5;
	constexpr double kFirstScientific = 1e16;

	int32_t FormatShortest(char* out, int32_t cap, double value)
	{
		const double magnitude = std::fabs(value);
		if (magnitude >= kSmallestPlain && magnitude < kFirstScientific)
		{
			const std::to_chars_result written = std::to_chars(out, out + cap - 1, value, std::chars_format::fixed);
			if (written.ec != std::errc()) return -1;
			*written.ptr = 0;
			return (int32_t)(written.ptr - out);
		}
		char scientific[kScientificCapacity];
		const std::to_chars_result written = std::to_chars(scientific, scientific + sizeof(scientific), value, std::chars_format::scientific);
		if (written.ec != std::errc()) return -1;
		const ShortestDigits shortest = Decompose(scientific, written.ptr);
		if (shortest.exponent < kLowestPlainExponent || shortest.exponent > kHighestPlainExponent)
			return Copied(out, cap, scientific, (size_t)(written.ptr - scientific));
		return WritePlain(out, cap, shortest);
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
