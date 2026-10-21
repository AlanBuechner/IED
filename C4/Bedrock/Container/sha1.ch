// Sha1: the SHA-1 digest (FIPS 180-4) - for protocols that require it, such as the WebSocket handshake. It is not a
// safe choice for anything that must resist a forger.
//     Core::Sha1Digest digest = Core::Sha1((const void*)text.Data(), text.ByteLength());

include "Text/string.ch"

namespace Core
{
	struct Sha1Digest
	{
		public uint8[20] bytes;

		// 40 lowercase hexadecimal digits.
		public String ToHex() const;
		[operator("==")] public bool Equals(const Sha1Digest& other) const;
		[operator("!=")] public bool NotEquals(const Sha1Digest& other) const { return !Equals(other); }
	}

	Sha1Digest Sha1(const void* bytes, int64 count);

	// The digest of a message given in pieces: Add as the bytes come, then Finish once.
	class Sha1Builder
	{
		public void Add(const void* bytes, int64 count);
		public Sha1Digest Finish();

		private void Compress(const uint8* block);

		private uint32[5] state;
		private uint8[64] block;
		private int64 blockBytes;
		private uint64 totalBytes;
		private bool started;
	}
}
