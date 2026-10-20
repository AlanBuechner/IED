// Base64 (RFC 4648, the standard alphabet with '=' padding): bytes as text and back.
//     Core::String text = Core::Base64Encode((const void*)bytes.Data(), bytes.Count());
//     Core::Array<uint8> back;
//     if (!Core::Base64Decode(text.View(), back)) ...

include "string.ch"
include "../Container/array.ch"

namespace Core
{
	String Base64Encode(const void* bytes, int64 count);

	// Appends the bytes `text` encodes to `out`. False, with `out` as it was, when `text` is not padded base64 (a length
	// that is not a multiple of 4, a character outside the alphabet, padding anywhere but the end).
	bool Base64Decode(StringView text, Array<uint8>& out);
}
