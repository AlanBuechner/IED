// Blake2b (RFC 7693): a fast cryptographic hash with a digest of 1 to 64 bytes and an optional key of up to 64 bytes;
// the hash Argon2 is built on.
//     uint8[32] digest;
//     Crypto::Blake2b((void*)&digest[0], 32, (const void*)text.Data(), text.ByteLength());

namespace Crypto
{
	const int32 kBlake2bMaxBytes = 64;

	// The `digestBytes`-byte digest (1 to 64) of `count` bytes, written to `out`.
	void Blake2b(void* out, int32 digestBytes, const void* bytes, int64 count);

	// The digest of a message given in pieces, keyed or not: Start, Add as the bytes come, then Finish once.
	class Blake2bBuilder
	{
		// False for a digest size outside 1 to 64 or a key longer than 64 bytes.
		public bool Start(int32 digestBytes, const void* key, int64 keyCount);
		public bool Start(int32 digestBytes) { return Start(digestBytes, nullptr, 0); }
		public void Add(const void* bytes, int64 count);
		// Writes the digest, the size Start was given, to `out`.
		public void Finish(void* out);

		private void Compress(bool last);

		private uint64[8] h;
		private uint8[128] block;
		private int64 blockBytes;
		private uint64 totalBytes;   // the low half of RFC 7693's 128-bit counter: no message here reaches 2^64 bytes
		private int32 digestBytes;
	}
}
