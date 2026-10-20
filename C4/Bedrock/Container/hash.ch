// Hashing for the containers: FNV-1a over bytes, a mixer for integers, and KeyTraits - how a HashMap
// hashes and compares keys of a type.

namespace Core
{
	// FNV-1a, 64-bit: xor each byte in, then multiply.
	uint64 HashBytes(const char* bytes, int64 length) {
		const uint8* p = (const uint8*)bytes;
		uint64 hash = 14695981039346656037u;
		for (int64 i = 0; i < length; ++i) hash = (hash ^ (uint64)p[i]) * 1099511628211u;
		return hash;
	}

	// The splitmix64 finalizer: spreads an integer's bits, so neighbouring keys land in different slots.
	uint64 HashInteger(uint64 value) {
		value = (value ^ (value >> 30)) * 13787848793156543929u;
		value = (value ^ (value >> 27)) * 10723151780598845931u;
		return value ^ (value >> 31);
	}

	// The smallest power of two that is at least `value` (and at least 1).
	int64 NextPowerOfTwo(int64 value) {
		int64 power = 1;
		while (power < value) power = power * 2;
		return power;
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

	// Hashes and compares a key's bytes: for plain data whose every byte is part of its value.
	template<K> struct ByteKeyTraits {
		public static uint64 Hash(const K& key) { return HashBytes((const char*)&key, (int64)sizeof(K)); }
		public static bool Equal(const K& a, const K& b) { return memcmp((void*)&a, (void*)&b, sizeof(K)) == 0; }
	}
}
