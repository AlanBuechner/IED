// Password strings: an Argon2 hash with its type, costs and salt in the PHC string format, so stored hashes can move to
// new costs one login at a time.
//     Core::String stored = Crypto::HashPassword(password, Crypto::DefaultArgon2Params());
//     if (Crypto::VerifyPassword(typed, stored.View())) ...

include "argon2.ch"

namespace Crypto
{
	// "$argon2id$v=19$m=65536,t=3,p=4$<salt>$<tag>" (unpadded base64) for a fresh 16-byte salt; empty when the
	// parameters are out of range or the system's random generator or the memory fails.
	Core::String HashPassword(Core::StringView password, const Argon2Params& params);
	// Whether `password` is the one `encoded` (HashPassword's form, any type) was made from; false for a malformed
	// string or one asking for more than 4 GiB.
	bool VerifyPassword(Core::StringView password, Core::StringView encoded);
}
