// Core::BitSet: a growable array of bits, worked on 64 at a time - set, clear and test single bits, count the set ones,
// find the next set or clear one, and combine sets word by word (std::vector<bool>, std::bitset). A zero BitSet holds
// no bits and allocates nothing; a copy owns its own words. Bit indices are unchecked, as element access is.
//     Core::BitSet visited(1000);         // 1000 clear bits
//     visited.Set(42);
//     for (int64 i = visited.NextSet(0); i >= 0; i = visited.NextSet(i + 1)) Visit(i);

namespace Core
{
	class BitSet
	{
	public:
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(int64 bits) { Resize(bits); }
		[operator("copy")] public void CopyFrom(const BitSet& other);
		[operator("delete")] public void Delete();

		public int64 Count() const { return bitCount; }   // how many bits it holds, set or not
		public bool IsEmpty() const { return bitCount == 0; }
		// Holds `bits` bits: the new ones are clear.
		public void Resize(int64 bits);

		public bool Test(int64 bit) const { return (words[bit >> 6] & ((uint64)1 << (bit & 63))) != 0u; }
		public void Set(int64 bit) { words[bit >> 6] |= (uint64)1 << (bit & 63); }
		public void Clear(int64 bit) { words[bit >> 6] &= ~((uint64)1 << (bit & 63)); }
		public void Flip(int64 bit) { words[bit >> 6] ^= (uint64)1 << (bit & 63); }
		public void Assign(int64 bit, bool value) { if (value) Set(bit); else Clear(bit); }
		public void SetAll();
		public void ClearAll();

		// How many bits are set; whether any, none or all are.
		public int64 CountSet() const;
		public bool Any() const { return NextSet(0) >= 0; }
		public bool None() const { return NextSet(0) < 0; }
		public bool All() const { return NextClear(0) < 0; }
		// The first set (or clear) bit at or after `from`, or -1.
		public int64 NextSet(int64 from) const;
		public int64 NextClear(int64 from) const;

		// Word by word with a set of the same size (a shorter `other` counts as clear past its end).
		[operator("&=")] public void And(const BitSet& other);
		[operator("|=")] public void Or(const BitSet& other);
		[operator("^=")] public void Xor(const BitSet& other);
		// Clears the bits `other` has set.
		public void AndNot(const BitSet& other);
		[operator("==")] public bool Equals(const BitSet& other) const;
		[operator("!=")] public bool NotEquals(const BitSet& other) const { return !Equals(other); }

	private:
		private int64 WordCount() const { return (bitCount + 63) >> 6; }
		// The bits of the last word past the end are kept clear, so counts and comparisons can read whole words.
		private void ClearTail();

		private uint64* words;
		private int64 bitCount;
		private int64 wordCapacity;
	}
}
