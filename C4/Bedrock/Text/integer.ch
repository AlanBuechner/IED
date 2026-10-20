// ParseInt64: decimal text as a number - an optional '-' and at least one digit, nothing else around them.
//     int64 port = 0;
//     if (!Core::ParseInt64(text, port)) ...

include "stringview.ch"

namespace Core
{
	// False, with `value` as it was, for any other text or a number outside int64.
	bool ParseInt64(StringView text, int64& value);
}
