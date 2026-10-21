// Core::Uuid: a 128-bit universally unique identifier (RFC 9562) - made at random (version 4) or ordered by time
// (version 7, which sorts by creation and keeps database indexes compact), read from and written as its 36-character
// text `8-4-4-4-12` hex digits. Making one takes the random generator (and the time), so a test can replay them.
//     Core::Random random(Core::RandomSeed());
//     Core::Uuid id = Core::Uuid::NewV4(random);
//     Core::Uuid ordered = Core::Uuid::NewV7(Platform::UnixTimeNs() / 1000000, random);
//     Core::String text = id.ToString();                  // "f47ac10b-58cc-4372-a567-0e02b2c3d479"

include "Random/random.ch"
include "stringbuilder.ch"

namespace Core
{
	struct Uuid
	{
		public uint8[16] bytes;   // in the order the text writes them

		public static Uuid NewV4(Random& random);
		// Version 7: the first 48 bits are `unixMilliseconds`, the rest random.
		public static Uuid NewV7(int64 unixMilliseconds, Random& random);
		// The 36-character form, either case, with or without surrounding braces; false (and `out` untouched) otherwise.
		public static bool Parse(StringView text, Uuid& out);

		public int32 Version() const { return (int32)(bytes[6] >> 4); }
		public bool IsNil() const;
		public void AppendTo(StringBuilder& out) const;   // lowercase, 36 characters
		public String ToString() const;

		[operator("==")] public bool Equals(const Uuid& other) const { return memcmp((void*)&bytes[0], (void*)&other.bytes[0], 16u) == 0; }
		[operator("!=")] public bool NotEquals(const Uuid& other) const { return !Equals(other); }
		[operator("<")] public bool Less(const Uuid& other) const { return memcmp((void*)&bytes[0], (void*)&other.bytes[0], 16u) < 0; }
		[operator("hash")] public uint64 Hash() const { return HashBytes((const char*)&bytes[0], 16); }
	}
}
