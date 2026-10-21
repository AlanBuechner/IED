// Values of reflected types to and from a DOM (D-72), by what their type names say they are: numbers and bool,
// Core::String, enums (by enumerator name), Core::Array<T>, fixed arrays T[N], Core::HashMap<Core::String, V> (a JSON
// object) and objects and unions (their fields, aliases skipped: a union through its first view), nested to any depth. A pointer is written as null and never
// read; a type with no such form (a func, a HashMap with other keys) is written as null and never read.
//     Serialization::DomDoc doc;
//     Serialization::DomNode tree = Serialization::WriteValue(doc, Serialization::DescribeValue(nameof(Config)), (const void*)&config);

include "dom.ch"
include "Container/array.ch"
include "Container/hashmap.ch"
include "Text/string.ch"

namespace Serialization
{
	enum class ValueKind { Unsupported, Pointer, Bool, Signed, Unsigned, Float, Double, String, Enum, Array, FixedArray, StringMap, Object }

	struct ValueType
	{
		public ValueKind kind;
		public Core::String name;        // the qualified type name (nameof, Reflection::Field::TypeName)
		public Reflection::Type type;    // registered types: objects, enums, arrays, maps
		public int64 size;
		public int64 align;
		public Core::String element;     // Array, FixedArray: the element type's name; StringMap: the value type's
		public int64 count;              // FixedArray: its length
		public int64 valueOffset;        // StringMap: where a KeyValue<Core::String, V>'s value starts
		public int64 entrySize;          // StringMap: a KeyValue's size and alignment
		public int64 entryAlign;
		public int64 entriesOffset;      // StringMap: where the map's entries pointer and its Core::HashTable are
		public int64 tableOffset;
	}

	// What the type named `name` is to the serializers.
	ValueType DescribeValue(const char* name);
	// The DOM form of the value of `type` at `at`.
	DomNode WriteValue(DomDoc& doc, const ValueType& type, const void* at);
	// `node` read into the value of `type` at `at`: an object's fields that `node` has (the rest kept), an array or map
	// that must be empty (its elements start zero, then read). False when `node` does not fit the type.
	bool ReadValue(const DomNode& node, const ValueType& type, void* at);
	// The fields of the object at `at` that `node` has, read: how many, or -1 when one did not fit.
	int32 ReadFields(const DomNode& node, Reflection::Type type, void* at);
}
