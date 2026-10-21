#pragma once
// The shortest decimal that reads back as a double, by Giulietti's Schubfach ("The Schubfach way to render doubles",
// 2020): three products with one 128-bit power of ten pick the candidates, no loop and no big numbers. The digits are
// the same as Ryu's (the closest of the shortest, ties to even).

#include <cstdint>

// A positive finite double as digits * 10^exponent, with the fewest significant digits that read back as it. The digits
// may end in zeros: the layout drops them from the text.
struct DecimalDouble
{
	uint64_t digits;
	int exponent;
};

DecimalDouble ShortestDecimal(double value);
