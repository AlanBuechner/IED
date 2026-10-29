// Hashing for the containers: a fast hash of bytes, a mixer for integers, and KeyTraits - how a HashMap or HashSet
// hashes and compares keys of a type.

namespace Core
{
	// A 64-bit hash of bytes in the manner of wyhash: 16 bytes a step (48 for long input), each pair of words folded
	// through a 128-bit product. Every bit of the result depends on every byte; not for anything that must resist a
	// forger (Crypto/ has those).
	uint64 HashBytes(const char* bytes, int64 length);

	// The 128-bit product of `a` and `b` with its halves xored: the mixing step of the byte hash.
	uint64 FoldedMultiply(uint64 a, uint64 b) { return a * b ^ mul_high(a, b); }

	// The splitmix64 finalizer: spreads an integer's bits, so neighbouring keys land in different slots.
	uint64 HashInteger(uint64 value) {
		value = (value ^ (value >> 30)) * 13787848793156543929u;
		value = (value ^ (value >> 27)) * 10723151780598845931u;
		return value ^ (value >> 31);
	}

	// A hash of several values from their hashes: `seed` with `value` mixed in, so the order counts (a hash of (a, b) is not
	// one of (b, a)). Start from any seed (0 or a first value's hash) and combine each value's hash in turn.
	uint64 HashCombine(uint64 seed, uint64 value) {
		return seed ^ (HashInteger(value) + 0x9e3779b97f4a7c15u + (seed << 6) + (seed >> 2));
	}

	// The smallest power of two that is at least `value` (and at least 1).
	int64 NextPowerOfTwo(int64 value) {
		if (value <= 1) return 1;
		return (int64)1 << (64 - bit_leading_zeros((uint64)(value - 1)));
	}

	// How a HashMap hashes and compares keys of type K. The primary asks K itself: its [operator("hash")]
	// and ==. Integers, characters, bool, floats and pointers have their own; ByteKeyTraits<K> covers a
	// plain-data struct without padding.
	template<K> struct KeyTraits {
		public static uint64 Hash(const K& key) { return key.operator_hash(); }
		public static bool Equal(const K& a, const K& b) { return a == b; }
	}
	template<> struct KeyTraits<int8> {
		public static uint64 Hash(int8 key) { return HashInteger((uint64)(int64)key); }
		public static bool Equal(int8 a, int8 b) { return a == b; }
	}
	template<> struct KeyTraits<int16> {
		public static uint64 Hash(int16 key) { return HashInteger((uint64)(int64)key); }
		public static bool Equal(int16 a, int16 b) { return a == b; }
	}
	template<> struct KeyTraits<int32> {
		public static uint64 Hash(int32 key) { return HashInteger((uint64)(int64)key); }
		public static bool Equal(int32 a, int32 b) { return a == b; }
	}
	template<> struct KeyTraits<int64> {
		public static uint64 Hash(int64 key) { return HashInteger((uint64)key); }
		public static bool Equal(int64 a, int64 b) { return a == b; }
	}
	template<> struct KeyTraits<uint8> {
		public static uint64 Hash(uint8 key) { return HashInteger((uint64)key); }
		public static bool Equal(uint8 a, uint8 b) { return a == b; }
	}
	template<> struct KeyTraits<uint16> {
		public static uint64 Hash(uint16 key) { return HashInteger((uint64)key); }
		public static bool Equal(uint16 a, uint16 b) { return a == b; }
	}
	template<> struct KeyTraits<uint32> {
		public static uint64 Hash(uint32 key) { return HashInteger((uint64)key); }
		public static bool Equal(uint32 a, uint32 b) { return a == b; }
	}
	template<> struct KeyTraits<uint64> {
		public static uint64 Hash(uint64 key) { return HashInteger(key); }
		public static bool Equal(uint64 a, uint64 b) { return a == b; }
	}
	template<> struct KeyTraits<char8> {
		public static uint64 Hash(char8 key) { return HashInteger((uint64)(uint8)key); }
		public static bool Equal(char8 a, char8 b) { return a == b; }
	}
	template<> struct KeyTraits<bool> {
		public static uint64 Hash(bool key) { return key ? 1u : 0u; }
		public static bool Equal(bool a, bool b) { return a == b; }
	}
	// Floats hash their bits, with -0 as +0 so equal keys hash alike; NaN never equals itself, as with ==.
	template<> struct KeyTraits<float> {
		public static uint64 Hash(float key) { float k = key == 0.0f ? 0.0f : key; return HashInteger((uint64)reinterpret_cast<uint32>(k)); }
		public static bool Equal(float a, float b) { return a == b; }
	}
	template<> struct KeyTraits<double> {
		public static uint64 Hash(double key) { double k = key == 0.0 ? 0.0 : key; return HashInteger(reinterpret_cast<uint64>(k)); }
		public static bool Equal(double a, double b) { return a == b; }
	}
	template<T> struct KeyTraits<T*> {
		public static uint64 Hash(T* key) { return HashInteger((uint64)key); }
		public static bool Equal(T* a, T* b) { return a == b; }
	}

	// How a lookup by a key of another type Q (HashMap::FindAs, HashSet::ContainsAs) hashes it for a table of K keys:
	// as Q's KeyTraits do, which must agree with K's (a StringView for String keys). A C string looking up String keys
	// hashes its text (Text/string.ch), not its address.
	template<K, Q> struct LookupTraits {
		public static uint64 Hash(const Q& key) { return KeyTraits<Q>::Hash(key); }
	}

	// Hashes and compares a key's bytes: for plain data whose every byte is part of its value.
	template<K> struct ByteKeyTraits {
		public static uint64 Hash(const K& key) { return HashBytes((const char*)&key, (int64)sizeof(K)); }
		public static bool Equal(const K& a, const K& b) { return memcmp((void*)&a, (void*)&b, sizeof(K)) == 0; }
	}
}
