// JSON text (RFC 8259) into a DOM. The reader is strict: what it accepts is exactly JSON - no comments,
// trailing commas, single quotes, leading zeros or NaN - plus a leading UTF-8 byte order mark, which it skips.
// A number without a fraction or exponent that fits an int64 is an Int, every other number a Float. An
// object keeps its members in order, duplicates included (Member finds the first).
//     Serialization::DomDoc doc;
//     Serialization::ReadResult read = Serialization::ReadJson(Core::StringView(text), doc);
//     if (!read.Ok()) printf("%d:%d %s\n", read.line, read.column, read.error);
//     int64 version = read.root.Member("version").AsInt();

include "readresult.ch"

namespace Serialization
{
	ReadResult ReadJson(Core::StringView text, DomDoc& doc);

	// Objects and arrays nested deeper than this are an error, so a hostile document cannot exhaust the stack.
	const int32 kMaxJsonDepth = 512;
}
