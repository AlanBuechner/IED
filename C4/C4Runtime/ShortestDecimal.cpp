#include "ShortestDecimal.h"
#include "PowersOfTen128.h"
#include <cstring>

namespace
{
	constexpr int kSignificandBits = 52;
	constexpr int kExponentBias = 1075; // 1023 plus the significand's bits: the value is c * 2^q
	constexpr uint64_t kHiddenBit = uint64_t(1) << kSignificandBits;

	struct Words
	{
		uint64_t high;
		uint64_t low;
	};

	Words Multiply(uint64_t a, uint64_t b)
	{
		const unsigned __int128 product = (unsigned __int128)a * b;
		return { (uint64_t)(product >> 64), (uint64_t)product };
	}

	// floor(log10(2^e)), floor(log10(3/4 * 2^e)) and floor(log2(10^e)), exact over the exponents a double has.
	int FloorLog10Pow2(int e) { return (e * 1262611) >> 22; }
	int FloorLog10ThreeQuartersPow2(int e) { return (e * 1262611 - 524031) >> 22; }
	int FloorLog2Pow10(int e) { return (e * 1741647) >> 19; }

	Words PowerOfTen(int k)
	{
		const int at = 2 * (k - kSmallestPowerOfTen);
		return { kPowersOfTen128[at], kPowersOfTen128[at + 1] };
	}

	// (g * cp) / 2^128, rounded to odd: the low bit says whether anything was dropped.
	uint64_t RoundToOdd(Words g, uint64_t cp)
	{
		const Words x = Multiply(g.low, cp);
		const Words y = Multiply(g.high, cp);
		const uint64_t z = y.low + x.high;
		const uint64_t high = y.high + (z < x.high ? 1 : 0);
		return high | (z > 1 ? 1 : 0);
	}

	uint64_t Select(bool condition, uint64_t ifTrue, uint64_t ifFalse)
	{
		const uint64_t mask = 0 - (uint64_t)condition;
		return (ifTrue & mask) | (ifFalse & ~mask);
	}
}

// Which candidate wins is a coin toss on ordinary data, so it is picked with masks, not branches.
DecimalDouble ShortestDecimal(double value)
{
	uint64_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	const uint64_t significand = bits & (kHiddenBit - 1);
	const uint64_t biased = (bits >> kSignificandBits) & 0x7ff;
	uint64_t c = significand;
	int q = 1 - kExponentBias;
	if (biased != 0)
	{
		c = kHiddenBit | significand;
		q = (int)biased - kExponentBias;
		if (q <= 0 && -q < kSignificandBits + 1 && (c & ((uint64_t(1) << -q) - 1)) == 0)
			return { c >> -q, 0 }; // an integer
	}
	const bool even = (c & 1) == 0;
	const bool lowerCloser = significand == 0 && biased > 1;
	const uint64_t cbl = 4 * c - 2 + (lowerCloser ? 1 : 0);
	const uint64_t cb = 4 * c;
	const uint64_t cbr = 4 * c + 2;
	const int k = lowerCloser ? FloorLog10ThreeQuartersPow2(q) : FloorLog10Pow2(q);
	const int h = q + FloorLog2Pow10(-k) + 1;
	const Words g = PowerOfTen(-k);
	const uint64_t vbl = RoundToOdd(g, cbl << h);
	const uint64_t vb = RoundToOdd(g, cb << h);
	const uint64_t vbr = RoundToOdd(g, cbr << h);
	const uint64_t lower = vbl + (even ? 0 : 1);
	const uint64_t upper = vbr - (even ? 0 : 1);
	const uint64_t s = vb / 4;
	const uint64_t sp = s / 10;
	const bool upInside = lower <= 40 * sp;
	const bool wpInside = 40 * sp + 40 <= upper;
	const bool shorter = (s >= 10) & (upInside != wpInside);
	const bool uInside = lower <= 4 * s;
	const bool wInside = 4 * s + 4 <= upper;
	const uint64_t middle = 4 * s + 2;
	const bool roundUp = (vb > middle) | ((vb == middle) & ((s & 1) != 0));
	const uint64_t nearest = s + ((uInside != wInside) ? wInside : roundUp);
	return { Select(shorter, sp + wpInside, nearest), k + (int)shorter };
}
