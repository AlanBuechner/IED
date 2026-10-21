// Core::Random: a fast generator of random numbers for games, simulations and tests - xoshiro256** (256 bits of state,
// a period of 2^256 - 1, passes the statistical test suites) seeded through splitmix64. Not for secrets: keys and
// tokens come from Platform::RandomBytes or Crypto. A generator is a value with no global one behind it: seed it and
// pass it to what needs numbers, so a run can be replayed from its seed.
//     Core::Random random(1234);                          // the same numbers from the same seed, on every machine
//     Core::Random fresh(Core::RandomSeed());             // seeded by the operating system
//     int64 roll = random.Range(1, 7);                    // 1 to 6
//     double t = random.Unit();                           // [0, 1)
//     random.Shuffle(cards);

include "Container/array.ch"

namespace Core
{
	struct Random
	{
		[operator("create")] public void Init(uint64 seed);

		// 64 random bits, and 32.
		public uint64 Next();
		public uint32 Next32() { return (uint32)(Next() >> 32); }
		// Uniform in [low, high), every value equally likely (no modulo bias); `low` when the range is empty.
		public int64 Range(int64 low, int64 high);
		// Uniform in [0, 1) with every double of the form k / 2^53, and in [0, 1) as a float (k / 2^24).
		public double Unit() { return (double)(Next() >> 11) * 1.1102230246251565e-16; }
		public float UnitFloat() { return (float)(Next() >> 40) * 5.9604645e-08f; }
		// Uniform in [low, high).
		public double RangeDouble(double low, double high) { return low + (high - low) * Unit(); }
		// True with the given probability (0: never, 1: always).
		public bool Chance(double probability) { return Unit() < probability; }
		// A normally distributed value (Marsaglia's polar method).
		public double Normal(double mean, double deviation);
		// Fisher-Yates: every order equally likely.
		template<T> public void Shuffle(T* items, int64 count) {
			for (int64 i = count - 1; i > 0; --i) {
				int64 j = Range(0, i + 1);
				if (j == i) continue;
				T held = take(items[i]);
				relocate(&items[i], &items[j]);
				relocate(&items[j], &held);
			}
		}
		template<T> public void Shuffle(Array<T>& items) { Shuffle(items.Data(), items.Count()); }
		// A generator whose numbers do not overlap this one's for 2^128 draws: this one jumps ahead and the
		// returned one starts where it was (for one generator per thread or per job).
		public Random Split();

		private uint64[4] state;
	}

	// A seed from the operating system's random source.
	uint64 RandomSeed();
}
