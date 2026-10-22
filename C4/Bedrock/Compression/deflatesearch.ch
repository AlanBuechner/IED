// Finding repeats for Deflate: how hard each level looks, the hash chains of the slower levels and the buckets of
// the fastest, and the longest match among their candidates.

include "deflate.ch"
include "deflatetables.ch"

namespace Compression
{
	// How hard a level looks (zlib's table): a match this long shortens the next search (good); a lazy level takes a
	// match without looking one byte on once it is this long (lazy), a greedy one inserts no more of a longer match's
	// positions; a match this long ends a search (nice); `chain` bounds the candidates a search tries.
	struct LevelSearch
	{
		public int32 good;
		public int32 lazy;
		public int32 nice;
		public int32 chain;
		public bool lazyMatching;
	}

	// The levels up to this one find matches with buckets instead of chains.
	const int32 kBucketLevel = 2;

	LevelSearch SearchFor(int32 level);

	// Positions are kept as 32-bit offsets from `base`, which moves up (zlib's slide) before they could overflow.
	const int64 kSlideAt = 1073741824;

	// A position is hashed by its next four bytes (zlib-ng's choice): a chain then holds only positions that share
	// four, so far fewer candidates are tried, at the cost of matches of exactly three bytes.
	const int64 kHashBytes = 4;

	// The positions seen, by the hash of their next four bytes: head[hash] is the latest (as its offset from `base`,
	// plus one; 0: none), and prev links a position to the one before it with the same hash, within the window.
	struct MatchFinder
	{
		public const uint8* data;
		public int64 size;
		public int64 base;
		public Core::Array<uint32> head;
		public Core::Array<uint32> prev;

		public void Start(const uint8* bytes, int64 count) {
			data = bytes;
			size = count;
			head.Resize((int64)kHashSize);
			prev.Resize(kWindowSize);
		}

		public void Insert(int64 position) {
			if (position + kHashBytes > size) return;
			if (position - base >= kSlideAt) Slide(position - kWindowSize - base);
			const int64 hash = Hash(position);
			prev[position & (kWindowSize - 1)] = head[hash];
			head[hash] = (uint32)(position - base + 1);
		}

		// The position an entry names, or -1 for none.
		public int64 Position(uint32 entry) const { return entry == 0u ? -1 : base + (int64)entry - 1; }

		private int64 Hash(int64 position) const {
			uint32 value = 0u;
			memcpy((void*)&value, (void*)(data + position), 4u);
			return (int64)((value * 2654435761u) >> (uint32)(32 - kHashBits));
		}

		// Every entry `shift` lower, those it would take below the base gone: none is within the window then.
		private void Slide(int64 shift) {
			SlideEntries(head.Data(), head.Count(), (uint32)shift);
			SlideEntries(prev.Data(), prev.Count(), (uint32)shift);
			base += shift;
		}
	}

	// How many bytes at `a` and `b` agree, up to `limit`: eight at a time.
	int32 CommonLength(const uint8* a, const uint8* b, int32 limit);

	uint32 Load32(const uint8* at);

	// The longest match for `position` (after it was inserted) longer than `shorter`, by its chain of candidates. A
	// candidate is compared in full only when its first four bytes and the four that would end a longer match agree
	// (zlib-ng's check): most are turned away by two loads.
	struct Match
	{
		public int32 length;
		public int32 distance;
	}

	Match LongestMatch(const MatchFinder& finder, int64 position, int32 shorter, const LevelSearch& search);

	// The fastest levels' match finder (libdeflate's ht_matchfinder): each hash keeps the last kBucketSize positions
	// that had it, newest first, and a position is matched against those alone. No chain is kept, so a position costs
	// one bucket's read and write.
	const int64 kBucketSize = 2;

	struct BucketFinder
	{
		public const uint8* data;
		public int64 size;
		public int64 base;
		public Core::Array<uint32> buckets;   // offsets from `base` plus one (0: none)

		public void Start(const uint8* bytes, int64 count) {
			data = bytes;
			size = count;
			buckets.Resize((int64)kHashSize * kBucketSize);
		}

		// The longest match among the bucket's positions for `position`, which then joins the bucket.
		public Match FindAndInsert(int64 position) {
			Match best;
			best.length = kMinMatch - 1;
			const int64 left = size - position;
			if (left < kHashBytes) return best;
			if (position - base >= kSlideAt) Slide(position - kWindowSize - base);
			const int32 limit = left < (int64)kMaxMatch ? (int32)left : kMaxMatch;
			const uint8* here = data + position;
			const uint32 start = Load32(here);
			uint32* bucket = buckets.Data() + Hash(position) * kBucketSize;
			for (int64 k = 0; k < kBucketSize; k++) {
				if (bucket[k] == 0u) break;
				const int64 candidate = base + (int64)bucket[k] - 1;
				if (position - candidate > kWindowSize) break;
				const uint8* there = data + candidate;
				if (Load32(there) != start) continue;
				const int32 length = CommonLength(there, here, limit);
				if (length > best.length) {
					best.length = length;
					best.distance = (int32)(position - candidate);
				}
			}
			Push(bucket, position);
			return best;
		}

		public void Insert(int64 position) {
			if (size - position < kHashBytes) return;
			Push(buckets.Data() + Hash(position) * kBucketSize, position);
		}

		private void Push(uint32* bucket, int64 position) {
			for (int64 k = kBucketSize - 1; k > 0; k--) bucket[k] = bucket[k - 1];
			bucket[0] = (uint32)(position - base + 1);
		}

		private int64 Hash(int64 position) const { return (int64)((Load32(data + position) * 2654435761u) >> (uint32)(32 - kHashBits)); }

		private void Slide(int64 shift) {
			SlideEntries(buckets.Data(), buckets.Count(), (uint32)shift);
			base += shift;
		}
	}
}
