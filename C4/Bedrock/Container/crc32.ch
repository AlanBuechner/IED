// CRC-32 (IEEE 802.3: the checksum of zlib, gzip, PNG and ZIP; reflected polynomial 0xEDB88320). It detects accidental
// changes, never deliberate ones (Crypto/ has those). Core::Crc32 at once, or a Crc32Builder fed in pieces, which gives
// the same value however the bytes are split.
//     uint32 sum = Core::Crc32(bytes, count);
//     Core::Crc32Builder running;  running.Add(header, 12);  running.Add(body, n);  uint32 total = running.Value();

namespace Core
{
	struct Crc32Builder
	{
		public void Add(const void* data, int64 count);
		public uint32 Value() const { return state; }

		private uint32 state;   // the CRC of what was added so far: zero for nothing
	}

	uint32 Crc32(const void* data, int64 count);
}
