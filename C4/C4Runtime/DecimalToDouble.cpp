#include "DecimalToDouble.h"
#include "PowersOfFive.h"
#include <bit>
#include <cstring>

namespace
{
	constexpr int kMostDigits = 19;              // what a uint64 holds exactly
	constexpr int64_t kMostExponent = 100000;    // past this the exponent's own digits are not read here
	constexpr int kMantissaBits = 52;
	constexpr int kMinimumExponent = -1023;
	constexpr int kInfinitePower = 0x7FF;
	constexpr int64_t kRoundToEvenLowest = -4;   // the powers of ten whose 5^q fits a word: only they can tie
	constexpr int64_t kRoundToEvenHighest = 23;
	constexpr uint64_t kClingerLargestMantissa = uint64_t(1) << 53;
	constexpr int64_t kClingerLargestPower = 22;
	constexpr double kExactPowersOfTen[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
		1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

	// The number the text spells: mantissa * 10^exponent.
	struct Decimal
	{
		uint64_t mantissa = 0;
		int64_t exponent = 0;
		bool negative = false;
		int64_t taken = 0;
	};

	bool IsDigit(const char* p, const char* end) { return p < end && *p >= '0' && *p <= '9'; }

	// Eight digits at once (SWAR): their bytes as one word, combined in pairs, quads and halves by three multiplies.
	bool EightDigits(const char* p, const char* end, uint64_t& word)
	{
		if (end - p < 8) return false;
		std::memcpy(&word, p, 8);
		return (((word & 0xF0F0F0F0F0F0F0F0ull) | (((word + 0x0606060606060606ull) & 0xF0F0F0F0F0F0F0F0ull) >> 4)) == 0x3333333333333333ull);
	}

	uint64_t EightDigitsValue(uint64_t word)
	{
		word = ((word & 0x0F0F0F0F0F0F0F0Full) * 2561) >> 8;
		word = ((word & 0x00FF00FF00FF00FFull) * 6553601) >> 16;
		return ((word & 0x0000FFFF0000FFFFull) * 42949672960001ull) >> 32;
	}

	// Runs of eight digits once the mantissa has started (leading zeros are counted one by one); how many were taken.
	int64_t AddEightDigitRuns(const char*& p, const char* end, Decimal& decimal, int& significant)
	{
		int64_t taken = 0;
		uint64_t word = 0;
		while (decimal.mantissa != 0 && significant + 8 <= kMostDigits && EightDigits(p, end, word))
		{
			decimal.mantissa = decimal.mantissa * 100000000 + EightDigitsValue(word);
			significant += 8;
			p += 8;
			taken += 8;
		}
		return taken;
	}

	// One digit into the mantissa; leading zeros are not significant. False past kMostDigits significant digits.
	bool AddDigit(Decimal& decimal, int& significant, char c)
	{
		if (decimal.mantissa == 0 && c == '0') return true;
		if (++significant > kMostDigits) return false;
		decimal.mantissa = decimal.mantissa * 10 + uint64_t(c - '0');
		return true;
	}

	// `e` or `E`, a sign and digits; nothing is taken when no digit follows (strtod reads "1e" as 1).
	bool ReadExponent(const char*& p, const char* end, Decimal& decimal)
	{
		if (p >= end || (*p != 'e' && *p != 'E')) return true;
		const char* at = p + 1;
		const bool negative = at < end && *at == '-';
		if (at < end && (*at == '-' || *at == '+')) ++at;
		if (!IsDigit(at, end)) return true;
		int64_t exponent = 0;
		for (; IsDigit(at, end); ++at)
		{
			if (exponent > kMostExponent) return false;
			exponent = exponent * 10 + (*at - '0');
		}
		decimal.exponent += negative ? -exponent : exponent;
		p = at;
		return true;
	}

	bool ReadDecimal(const char* text, int64_t length, Decimal& decimal)
	{
		const char* p = text;
		const char* end = text + length;
		if (p < end && *p == '-') { decimal.negative = true; ++p; }
		int significant = 0;
		const bool integerDigits = IsDigit(p, end);
		for (; IsDigit(p, end); ++p)
		{
			AddEightDigitRuns(p, end, decimal, significant);
			if (!IsDigit(p, end)) break;
			if (!AddDigit(decimal, significant, *p)) return false;
		}
		if (p < end && *p == '.' && (integerDigits || IsDigit(p + 1, end)))
		{
			for (++p; IsDigit(p, end); ++p)
			{
				decimal.exponent -= AddEightDigitRuns(p, end, decimal, significant);
				if (!IsDigit(p, end)) break;
				if (!AddDigit(decimal, significant, *p)) return false;
				decimal.exponent -= 1;
			}
		}
		else if (!integerDigits) return false;
		if (!ReadExponent(p, end, decimal)) return false;
		decimal.taken = p - text;
		return true;
	}

	double FromBits(bool negative, uint64_t mantissa, int64_t power2)
	{
		const uint64_t bits = mantissa | (uint64_t(power2) << kMantissaBits) | (negative ? uint64_t(1) << 63 : 0);
		double value = 0;
		std::memcpy(&value, &bits, sizeof(value));
		return value;
	}

	struct Product { uint64_t low; uint64_t high; };

	Product Multiply(uint64_t a, uint64_t b)
	{
		const unsigned __int128 product = (unsigned __int128)a * b;
		return { uint64_t(product), uint64_t(product >> 64) };
	}

	// The high 128 bits of w * 5^q, the second word of the power only when the first leaves the low bits that decide
	// the rounding all ones.
	Product PowerProduct(int64_t q, uint64_t w)
	{
		const int64_t index = 2 * (q - kSmallestPowerOfFive);
		Product first = Multiply(w, kPowersOfFive[index]);
		constexpr uint64_t precisionMask = ~uint64_t(0) >> (kMantissaBits + 3);
		if ((first.high & precisionMask) == precisionMask)
		{
			const Product second = Multiply(w, kPowersOfFive[index + 1]);
			first.low += second.high;
			if (second.high > first.low) ++first.high;
		}
		return first;
	}

	// floor(q * log2(10)) + 63.
	int64_t BinaryPower(int64_t q) { return (((152170 + 65536) * q) >> 16) + 63; }

	// w * 10^q as a double's mantissa and biased exponent (Eisel-Lemire); w is not zero and q is in the table.
	void EiselLemire(int64_t q, uint64_t w, uint64_t& mantissa, int64_t& power2)
	{
		const int leadingZeros = std::countl_zero(w);
		w <<= leadingZeros;
		const Product product = PowerProduct(q, w);
		const int upperBit = int(product.high >> 63);
		const int shift = upperBit + 64 - kMantissaBits - 3;
		mantissa = product.high >> shift;
		power2 = BinaryPower(q) + upperBit - leadingZeros - kMinimumExponent;
		if (power2 <= 0)
		{
			if (-power2 + 1 >= 64) { mantissa = 0; power2 = 0; return; }
			mantissa >>= -power2 + 1;
			mantissa += mantissa & 1;
			mantissa >>= 1;
			power2 = mantissa < (uint64_t(1) << kMantissaBits) ? 0 : 1;   // a subnormal that rounds up to normal
			return;
		}
		// Halfway between two doubles, with nothing dropped: round to even.
		if (product.low <= 1 && q >= kRoundToEvenLowest && q <= kRoundToEvenHighest && (mantissa & 3) == 1 && (mantissa << shift) == product.high)
			mantissa &= ~uint64_t(1);
		mantissa += mantissa & 1;
		mantissa >>= 1;
		if (mantissa >= (uint64_t(2) << kMantissaBits))
		{
			mantissa = uint64_t(1) << kMantissaBits;
			power2 += 1;
		}
		mantissa &= ~(uint64_t(1) << kMantissaBits);
		if (power2 >= kInfinitePower) { power2 = kInfinitePower; mantissa = 0; }
	}

	double ToDouble(const Decimal& decimal)
	{
		const int64_t q = decimal.exponent;
		const uint64_t w = decimal.mantissa;
		if (w == 0 || q < kSmallestPowerOfFive) return FromBits(decimal.negative, 0, 0);
		if (q > kLargestPowerOfFive) return FromBits(decimal.negative, 0, kInfinitePower);
		if (w <= kClingerLargestMantissa && q >= -kClingerLargestPower && q <= kClingerLargestPower)
		{
			const double exact = q < 0 ? double(w) / kExactPowersOfTen[-q] : double(w) * kExactPowersOfTen[q];
			return decimal.negative ? -exact : exact;
		}
		uint64_t mantissa = 0;
		int64_t power2 = 0;
		EiselLemire(q, w, mantissa, power2);
		return FromBits(decimal.negative, mantissa, power2);
	}
}

int64_t ParseDecimalFast(const char* text, int64_t length, double* out)
{
	Decimal decimal;
	if (!ReadDecimal(text, length, decimal)) return 0;
	*out = ToDouble(decimal);
	return decimal.taken;
}
