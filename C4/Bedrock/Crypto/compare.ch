// ConstantTimeEquals: whether two runs of bytes match, taking the same time wherever they differ - for checking a
// secret (a token, a digest, a tag) without telling a guesser how much of the guess was right.
//     if (!Crypto::ConstantTimeEquals((const void*)&sent.bytes[0], (const void*)&kept.bytes[0], 32)) ...

namespace Crypto
{
	bool ConstantTimeEquals(const void* a, const void* b, int64 count);
}
