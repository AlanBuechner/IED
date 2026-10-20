// Sha256: the SHA-256 digest (FIPS 180-4) - content hashes, stored digests of secrets, the hash under HMAC.
//     Crypto::Sha256Digest digest = Crypto::Sha256((const void*)text.Data(), text.ByteLength());

include "../Text/string.ch"

namespace Crypto
{
	const int32 kSha256Bytes = 32;

	struct Sha256Digest
	{
		public uint8[32] bytes;

		// 64 lowercase hexadecimal digits.
		public Core::String ToHex() const;
		// Takes the same time wherever the digests differ (ConstantTimeEquals).
		[operator("==")] public bool Equals(const Sha256Digest& other) const;
		[operator("!=")] public bool NotEquals(const Sha256Digest& other) const { return !Equals(other); }
	}

	Sha256Digest Sha256(const void* bytes, int64 count);

	// The digest of a message given in pieces: Add as the bytes come, then Finish once.
	class Sha256Builder
	{
		public void Add(const void* bytes, int64 count);
		public Sha256Digest Finish();

		private void Compress(const uint8* block);

		private uint32[8] state;
		private uint8[64] block;
		private int64 blockBytes;
		private uint64 totalBytes;
		private bool started;
	}
}
