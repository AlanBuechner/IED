// The shortest text of a double: Schubfach's digits (ShortestDecimal.h) widened to seventeen, converted sixteen at a
// time in SSE2 lanes, and stored with the point already in place, so no byte is moved twice and nothing branches on
// the digits.

#include "ShortestDouble.h"
#include "ShortestDecimal.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <emmintrin.h>

namespace
{
	// The exponents a shortest number is written plainly between (`1000`, `0.001`); outside them it is scientific
	// (`1e+16`, `1e-05`) - the choice Python's repr makes.
	constexpr int kLowestPlainExponent = -5;
	constexpr int kHighestPlainExponent = 15;
	constexpr int kMostDigits = 17;
	constexpr int kLaneDigits = 16;
	const char kDigitPairs[] = "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
	constexpr uint64_t kPowersOfTen[] = { 1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull,
		1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull, 100000000000000ull, 1000000000000000ull,
		10000000000000000ull, 100000000000000000ull, 1000000000000000000ull, 10000000000000000000ull };

	// Seventeen digits: the first sixteen as byte values 0-9 (not yet text), and the last.
	struct Digits
	{
		__m128i first16;
		uint32_t last;
		int significant; // the digits before the trailing zeros
	};

	// A decimal's digits widened to exactly seventeen, with the exponent of its first digit.
	struct WidenedDecimal
	{
		uint64_t digits;
		int leading;
	};

	int DigitCount(uint64_t value)
	{
		const int guess = ((64 - std::countl_zero(value)) * 1233) >> 12;
		return guess + (value >= kPowersOfTen[guess] ? 1 : 0);
	}

	// A normal double's shortest digits number 15 to 17, so two comparisons count them; others walk the table.
	WidenedDecimal Widened(DecimalDouble decimal)
	{
		if (decimal.digits >= kPowersOfTen[15]) [[likely]]
		{
			const bool sixteen = decimal.digits < kPowersOfTen[16];
			const int count = kMostDigits - (sixteen ? 1 : 0);
			return { sixteen ? decimal.digits * 10 : decimal.digits, decimal.exponent + count - 1 };
		}
		const int count = DigitCount(decimal.digits);
		return { decimal.digits * kPowersOfTen[kMostDigits - count], decimal.exponent + count - 1 };
	}

	// The eight digits of `value` (< 10^8) in eight 16-bit lanes (Mula's SSE2 conversion): the halves by 10^4, each
	// half's prefixes a, ab, abc, abcd by multiply-high, then each digit as its prefix less ten times the one before.
	__m128i EightDigitLanes(uint32_t value)
	{
		const __m128i whole = _mm_cvtsi32_si128((int)value);
		const __m128i high = _mm_srli_epi64(_mm_mul_epu32(whole, _mm_set1_epi32((int)0xd1b71759)), 45);
		const __m128i low = _mm_sub_epi32(whole, _mm_mul_epu32(high, _mm_set1_epi32(10000)));
		const __m128i halves = _mm_slli_epi64(_mm_unpacklo_epi16(high, low), 2);
		const __m128i pairs = _mm_unpacklo_epi16(halves, halves);
		const __m128i spread = _mm_unpacklo_epi32(pairs, pairs);
		const __m128i scaled = _mm_mulhi_epu16(spread, _mm_setr_epi16(8389, 5243, 13108, (short)32768, 8389, 5243, 13108, (short)32768));
		const __m128i prefixes = _mm_mulhi_epu16(scaled, _mm_setr_epi16(1 << 7, 1 << 11, 1 << 13, (short)(1 << 15), 1 << 7, 1 << 11, 1 << 13, (short)(1 << 15)));
		const __m128i tens = _mm_slli_epi64(_mm_mullo_epi16(prefixes, _mm_set1_epi16(10)), 16);
		return _mm_sub_epi16(prefixes, tens);
	}

	// The digits of `widened` (10^16 <= widened < 10^17); the trailing zeros counted from the zero lanes.
	Digits SeventeenDigits(uint64_t widened)
	{
		const uint64_t sixteen = widened / 10;
		const uint32_t high = (uint32_t)(sixteen / 100000000);
		const uint32_t low = (uint32_t)(sixteen - (uint64_t)high * 100000000);
		Digits digits;
		digits.last = (uint32_t)(widened - sixteen * 10);
		digits.first16 = _mm_packus_epi16(EightDigitLanes(high), EightDigitLanes(low));
		const uint32_t zeroLanes = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(digits.first16, _mm_setzero_si128()));
		const int zerosEndingSixteen = std::countl_zero(~(zeroLanes << kLaneDigits) | (1u << (kLaneDigits - 1)));
		digits.significant = digits.last != 0 ? kMostDigits : kMostDigits - 1 - zerosEndingSixteen;
		return digits;
	}

	__m128i AsText(__m128i digitLanes) { return _mm_add_epi8(digitLanes, _mm_set1_epi8('0')); }

	// The digits with a point after the first `integerDigits` (1 to 16): the head as is, the tail one byte on, stored
	// whole. The point lands past the text's end when every digit is in the integer part.
	void WriteWithPoint(char* at, const Digits& digits, int integerDigits)
	{
		const __m128i text = AsText(digits.first16);
		const __m128i lanes = _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
		const __m128i point = _mm_set1_epi8((char)integerDigits);
		const __m128i head = _mm_cmpgt_epi8(point, lanes);
		const __m128i isPoint = _mm_cmpeq_epi8(point, lanes);
		const __m128i tail = _mm_andnot_si128(_mm_or_si128(head, isPoint), _mm_slli_si128(text, 1));
		const __m128i dot = _mm_and_si128(isPoint, _mm_set1_epi8('.'));
		_mm_storeu_si128((__m128i*)at, _mm_or_si128(_mm_or_si128(_mm_and_si128(head, text), tail), dot));
		at[kLaneDigits] = integerDigits == kLaneDigits ? '.' : (char)(_mm_extract_epi16(text, 7) >> 8);
		at[kLaneDigits + 1] = (char)('0' + digits.last);
	}

	// `0.000ddd`: the zeros after the point, then the digits.
	int WriteBelowOne(char* at, const Digits& digits, int leading)
	{
		std::memcpy(at, "0.000000", 8);
		const int prefix = 1 - leading;
		_mm_storeu_si128((__m128i*)(at + prefix), AsText(digits.first16));
		at[prefix + kLaneDigits] = (char)('0' + digits.last);
		return prefix + digits.significant;
	}

	// `e`, a sign and at least two digits, as charconv writes a scientific exponent.
	int WriteExponent(char* at, int exponent)
	{
		at[0] = 'e';
		at[1] = exponent < 0 ? '-' : '+';
		const int magnitude = exponent < 0 ? -exponent : exponent;
		if (magnitude < 100)
		{
			std::memcpy(at + 2, kDigitPairs + magnitude * 2, 2);
			return 4;
		}
		at[2] = (char)('0' + magnitude / 100);
		std::memcpy(at + 3, kDigitPairs + (magnitude % 100) * 2, 2);
		return 5;
	}

	int WriteScientific(char* at, const Digits& digits, int leading)
	{
		WriteWithPoint(at, digits, 1);
		const int mantissa = digits.significant > 1 ? digits.significant + 1 : 1;
		return mantissa + WriteExponent(at + mantissa, leading);
	}

	int WritePlain(char* at, const Digits& digits, int leading)
	{
		const int integerDigits = leading + 1;
		WriteWithPoint(at, digits, integerDigits);
		return digits.significant <= integerDigits ? integerDigits : digits.significant + 1;
	}

	int WriteMagnitude(char* at, double magnitude)
	{
		if (magnitude == 0)
		{
			*at = '0';
			return 1;
		}
		const WidenedDecimal decimal = Widened(ShortestDecimal(magnitude));
		const Digits digits = SeventeenDigits(decimal.digits);
		if (decimal.leading < kLowestPlainExponent || decimal.leading > kHighestPlainExponent)
			return WriteScientific(at, digits, decimal.leading);
		if (decimal.leading < 0) return WriteBelowOne(at, digits, decimal.leading);
		return WritePlain(at, digits, decimal.leading);
	}
}

int WriteShortestDouble(char* text, double value)
{
	uint64_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	const int sign = (int)(bits >> 63);
	text[0] = '-'; // overwritten by the digits when there is no sign
	bits &= ~(uint64_t(1) << 63);
	double magnitude = 0;
	std::memcpy(&magnitude, &bits, sizeof(magnitude));
	return sign + WriteMagnitude(text + sign, magnitude);
}
