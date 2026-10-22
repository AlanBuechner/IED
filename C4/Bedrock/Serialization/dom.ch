// DomDoc: an in-memory tree of values - null, bool, integer, float, string, object (ordered members) and
// array - that the JSON and YAML writers and the reflection bridge share. The document owns
// every node in one arena; a DomNode is a {document, index} handle, cheap to copy and valid while the
// document lives. String bytes live in the document's pool: AsString / AsView are valid until the next
// string or key is added.
//     Serialization::DomDoc doc;
//     Serialization::DomNode root = doc.NewObject();
//     doc.AddMember(root, "name", doc.NewString("c4"));
//     int64 version = root.Member("version").AsInt();   // 0: a missing member is an invalid node

include "Text/stringbuilder.ch"
include "Reflection/reflect.ch"

namespace Serialization
{
	class DomDoc;

	enum class DomKind { Null, Bool, Int, Float, String, Object, Array }

	// One node of the arena. String: `count` bytes at `first` in the pool. Object: `count` members at `first` in the
	// document's children - [key offset, key length, value index] each - with room for `capacity` ints there. Array:
	// `count` element indices at `first`. Bool, Int and Float: `bits` holds the value (a float's bits).
	struct DomRecord {
		public int64 bits;
		public DomKind kind;
		public int32 first;
		public int32 count;
		public int32 capacity;
	}

	// The ints of one object member in the children: the key's offset and length in the pool, the value's index.
	const int32 kMemberInts = 3;

	// A handle to one node. An invalid node (a missing member, an index out of range) reads as Null.
	struct DomNode {
		public DomDoc* doc;
		public int32 index = -1;

		public bool IsValid() const;
		public DomKind Kind() const;          // Null for an invalid node
		public bool IsNull() const { return Kind() == DomKind::Null; }
		public int64 AsInt() const;           // a Float truncates; 0 for anything else
		public double AsFloat() const;        // an Int converts; 0 for anything else
		public bool AsBool() const;           // false for anything but a true Bool
		public const char* AsString() const;  // null-terminated; "" for anything but a String
		public Core::StringView AsView() const;
		public int64 Count() const;           // members of an Object, elements of an Array, else 0

		public DomNode Member(const char* key) const;   // an invalid node when absent
		public Core::StringView KeyAt(int64 i) const;
		public DomNode ValueAt(int64 i) const;           // the i-th member's value
		public DomNode At(int64 i) const;                // the i-th element of an Array
	}

	class DomDoc
	{
	public:
		[operator("create")] public void Init() {}
		[operator("delete")] public void Delete();
		// The arena: records, string pool and children.
		[operator("native_bytes")] public int64 NativeBytes() const {
			return (int64)recordCapacity * (int64)sizeof(DomRecord) + (int64)poolCapacity + (int64)childCapacity * (int64)sizeof(int32);
		}

		public DomNode NewNull() { return Append(DomKind::Null, 0); }
		public DomNode NewBool(bool value) { return Append(DomKind::Bool, value ? 1 : 0); }
		public DomNode NewInt(int64 value) { return Append(DomKind::Int, value); }
		public DomNode NewFloat(double value) { return Append(DomKind::Float, reinterpret_cast<int64>(value)); }
		public DomNode NewString(const char* text) { return NewString(Core::StringView(text)); }
		public DomNode NewString(Core::StringView text);
		public DomNode NewObject() { return Append(DomKind::Object, 0); }
		public DomNode NewArray() { return Append(DomKind::Array, 0); }

		public void AddMember(const DomNode& object, const char* key, const DomNode& value) { AddMember(object, Core::StringView(key), value); }
		public void AddMember(const DomNode& object, Core::StringView key, const DomNode& value);
		public void Add(const DomNode& array, const DomNode& value);

		// A whole container at once, for a reader that collects the children while it reads them: an object of
		// `count` members, [Key(name), name length, value index] each, or an array of `count` element indices.
		public DomNode NewObjectOf(const int32* members, int64 count);
		public DomNode NewArrayOf(const int32* elements, int64 count);
		// A member's name in the pool, for NewObjectOf: its offset.
		public int32 Key(Core::StringView name) { return Intern(name); }

		public int64 NodeCount() const { return recordCount; }
		// Every node gone (their handles become invalid); the arena keeps its memory for the nodes that follow.
		public void Clear();

		// Reflection bridge (reflectvalue.ch): a node tree of a reflected object, and an object's members applied to
		// one (missing members are left alone). ReflectTo returns how many fields it set, or -1 when a member does not
		// fit its field.
		public DomNode ReflectFrom(Reflection::Type type, const void* object);
		public int32 ReflectTo(Reflection::Type type, const DomNode& object, void* destination);

		// The arena, read by DomNode (C4 has no friend). Every container's children are ints in one block, so a document
		// of any size is three allocations.
		public DomRecord* records;
		public int32 recordCount;
		public char* pool;
		public int32* children;

	private:
		private DomNode Append(DomKind kind, int64 bits);
		private int32 Intern(Core::StringView text);   // the bytes, null-terminated, into the pool; their offset
		private void ReserveRecords(int64 count);
		private void ReservePool(int64 bytes);
		private void ReserveChildArena(int64 ints);
		// Room for `ints` children ints in `record`'s block: grown where it ends the arena, else moved to its end.
		private void ReserveChildren(int32 index, int64 ints);
		// `count` ints appended to the arena: their first.
		private int32 AppendChildren(const int32* ints, int64 count);

		private int32 recordCapacity;
		private int32 poolLength;
		private int32 poolCapacity;
		private int32 childLength;
		private int32 childCapacity;
	}
}
