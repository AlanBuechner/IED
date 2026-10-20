// HmacSha256: a message authentication code (RFC 2104 over SHA-256). Only a holder of the key can make the tag of a
// message, so a tag that checks out says the message came from one and was not changed.
//     Crypto::Sha256Digest tag = Crypto::HmacSha256(key.Data(), key.Count(), (const void*)text.Data(), text.ByteLength());

include "sha256.ch"

namespace Crypto
{
	Sha256Digest HmacSha256(const void* key, int64 keyCount, const void* message, int64 count);
}
