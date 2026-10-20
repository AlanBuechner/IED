// A reflected object's fields as bytes: each field in declaration order, a nested object recursively and
// any other field as its raw bytes. Plain data only (no pointers or owning types); the layout comes from
// the reflection registry, so a new field needs no change here.

include "../Text/stringbuilder.ch"
include "../Reflection/reflect.ch"

namespace Serialization
{
	// Appends `object`'s bytes to `out`; how many, or -1 when `type` is not a reflected type.
	int64 WriteBinary(Reflection::Type type, const void* object, Core::StringBuilder* out);

	// Reads the bytes WriteBinary wrote, from `position` in `bytes`, into `destination`. Returns the position
	// after them, or -1 when `type` is not a reflected type or the bytes end too soon.
	int64 ReadBinary(Core::StringView bytes, int64 position, Reflection::Type type, void* destination);
}
