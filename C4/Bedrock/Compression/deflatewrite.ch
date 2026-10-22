// Writing Deflate's blocks: the symbols found, bits written least significant first, and each block stored,
// fixed-coded or coded with its own codes, whichever is smallest.

include "deflatecodes.ch"
include "Container/array.ch"

namespace Compression
{
	// A block's literals and matches as they were found, and the input bytes they cover.
	struct Symbols
	{
		public Core::Array<uint32> items;   // a literal byte, or kMatchFlag | length << 15 | (distance - 1)
		public int64 start;                 // the input offset the block begins at
		public int64 end;

		public Frequencies counts;          // of the symbols, counted as they come

		public void Literal(uint8 byte) {
			items.Push((uint32)byte);
			counts.literals[(int32)byte] += 1u;
			end += 1;
		}

		public void Match(int32 length, int32 distance) {
			items.Push(kMatchFlag | ((uint32)length << 15) | (uint32)(distance - 1));
			counts.literals[257 + LengthIndex(length)] += 1u;
			counts.distances[DistanceSymbol(distance)] += 1u;
			end += (int64)length;
		}
	}

	// The most bytes a block's coded symbols and header take besides 6 a symbol (a 15-bit code, 5 extra bits, a 15-bit
	// code and 13 extra bits): the header, the end of block and the last bits.
	const int64 kBlockRoomSlack = 1024;

	// Bits out, least significant first, after what `out` held: whole 32-bit words stored straight into room Reserve
	// made, which Finish trims to what was written. Writing loops copy it into a local, so a stored byte (which may
	// alias anything) does not make them read its fields again.
	struct BitWriter
	{
		public Core::Array<uint8>* out;
		public uint8* base;     // out's bytes, until the next Reserve
		public int64 written;   // bytes of `out` written
		public uint64 bits;
		public int32 count;

		public void Start(Core::Array<uint8>& array) {
			out = &array;
			written = array.Count();
			base = array.Data();
		}

		// Room for `bytes` more bytes past what was written.
		public void Reserve(int64 bytes) {
			const int64 needed = written + bytes;
			const int64 size = out->Count();
			if (needed > size) out->Resize(needed > size * 2 ? needed : size * 2);
			base = out->Data();
		}

		// At most 32 bits a call: whole 32-bit words go out as they fill.
		public void Put(uint32 value, int32 n) {
			bits |= (uint64)value << (uint64)count;
			count += n;
			if (count < 32) return;
			const uint32 word = (uint32)bits;
			memcpy((void*)(base + written), (void*)&word, 4u);
			written += 4;
			bits >>= 32u;
			count -= 32;
		}

		// To the next whole byte, with zero bits, and the whole bytes out.
		public void Align() {
			if ((count & 7) != 0) Put(0u, 8 - (count & 7));
			while (count > 0) {
				base[written] = (uint8)(bits & 0xffu);
				written += 1;
				bits >>= 8u;
				count -= 8;
			}
		}

		// Bytes as they are, once aligned.
		public void PutBytes(const uint8* bytes, int64 n) {
			memcpy((void*)(base + written), (void*)bytes, (uint64)n);
			written += n;
		}

		// The last bits out, and `out` exactly what was written.
		public void Finish() {
			Align();
			out->Resize(written);
		}
	}

	void WriteSymbols(BitWriter& into, const Symbols& symbols, const CodeSet& codes);

	void WriteHeader(BitWriter& writer, const DynamicHeader& header);

	void WriteStored(BitWriter& writer, const uint8* data, int64 start, int64 end, bool last);

	int64 StoredBits(int64 bytes, int32 pending);

	// The block in whichever form takes fewest bits: stored, fixed codes, or codes of its own.
	void WriteBlock(BitWriter& writer, const uint8* data, const Symbols& symbols, bool last);
}
