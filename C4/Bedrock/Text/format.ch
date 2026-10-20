// Format: positional formatting into a caller-owned StringBuilder.
//
// A placeholder is {index} or {index:kind.width}. Indices name the arguments in call order, may come in
// any order and may repeat; `{{` and `}}` are literal braces. Up to kFormatArgumentLimit arguments.
//
//   kind  argument       written as                       .N
//   d     int32 (default) decimal                          zero-pad to N digits
//   D     int64          decimal                          zero-pad to N digits
//   u     uint64         decimal                          zero-pad to N digits
//   x     uint64         hexadecimal, lowercase           zero-pad to N digits
//   b     uint64         binary                           zero-pad to N digits
//   o     uint64         octal                            zero-pad to N digits
//   f     float          fixed point                      N fraction digits (default 0)
//   F     double         fixed point                      N fraction digits (default 0)
//   s     const char*    the null-terminated text         -
//
//   Core::Format(&b, "id={0:d.4} val={1:x.8} again={0}", 255, 4096);   // "id=0255 val=00001000 again=255"
//
// An argument no placeholder names is skipped as an 8-byte value; a malformed placeholder is written as
// text.

include "stringbuilder.ch"

namespace Core
{
	const int32 kFormatArgumentLimit = 16;

	void Format(StringBuilder* out, const char* format, ...);
}
