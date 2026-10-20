// Argon2 (RFC 9106): memory-hard password hashing - each guess costs an attacker the memory and the passes it cost the
// owner. Argon2id is the type for passwords; password.ch keeps a hash with its costs and salt as one string.
//     Crypto::Argon2(Crypto::DefaultArgon2Params(), input, (void*)&tag[0]);

include "../Text/string.ch"

namespace Crypto
{
	enum class Argon2Type { D, I, Id }   // RFC 9106's y: 0, 1, 2

	struct Argon2Params
	{
		public Argon2Type type;
		public int32 memoryKiB;   // m: at least 8 per lane
		public int32 passes;      // t: at least 1
		public int32 lanes;       // p: 1 to 2^24 - 1; computed one after another, so they cost time, not threads
		public int32 tagBytes;    // T: at least 4
	}

	// RFC 9106's second recommended option: Argon2id, 64 MiB, 3 passes, 4 lanes, a 32-byte tag.
	Argon2Params DefaultArgon2Params();

	// The bytes hashed: the password, a salt of at least 8 bytes, and the optional secret key and associated data.
	struct Argon2Input
	{
		public const void* password;
		public int64 passwordBytes;
		public const void* salt;
		public int64 saltBytes;
		public const void* secret;
		public int64 secretBytes;
		public const void* associated;
		public int64 associatedBytes;
	}

	// Writes the params.tagBytes-byte tag to `tag`; false when a cost or the salt is out of range or the memory cannot
	// be had.
	bool Argon2(const Argon2Params& params, const Argon2Input& input, void* tag);
}
