// DomDoc: an in-memory tree of values - null, bool, integer, float, string, object (ordered members) and
// array - that the JSON and YAML writers and the reflection bridge share. The document owns
// every node in one arena; a DomNode is a {document, index} handle, cheap to copy and valid while the
// document lives. String bytes live in the document's pool: AsString / AsView are valid until the next
// string or key is added.
//     Serialization::DomDoc doc;
//     Serialization::DomNode root = doc.NewObject();
//     doc.AddMember(root, "name", doc.NewString("c4"));
//     int64 version = root.Member("version").AsInt();   // 0: a missing member is an invalid node

include "../Text/stringbuilder.ch"
include "../Reflection/reflect.ch"

namespace Serialization
{
	class DomDoc;

	enum class DomKind { Null, Bool, Int, Float, String, Object, Array }

	// One node of the arena. String: `offset` and `count` are its bytes in the pool. Object: `count` members,
	// `children` holds [key offset, key length, value index] per member. Array: `count` elements, `children`
	// holds their indices. Bool, Int and Float: `bits` holds the value (a float's bits).
	struct DomRecord {
		public DomKind kind;
		public int64 bits;
		public int32 offset;
		public int32 count;
		public int32* children;
		public int32 childCapacity;   // ints
	}

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

		public int64 NodeCount() const { return recordCount; }

		// Reflection bridge (reflectvalue.ch): a node tree of a reflected object, and an object's members applied to
		// one (missing members are left alone). ReflectTo returns how many fields it set, or -1 when a member does not
		// fit its field.
		public DomNode ReflectFrom(Reflection::Type type, const void* object);
		public int32 ReflectTo(Reflection::Type type, const DomNode& object, void* destination);

		// The arena, read by DomNode (C4 has no friend).
		public DomRecord* records;
		public int32 recordCount;
		public char* pool;

	private:
		private DomNode Append(DomKind kind, int64 bits);
		private int32 Intern(Core::StringView text);   // the bytes, null-terminated, into the pool; their offset
		private void ReserveRecords(int64 count);
		private void ReserveChildren(DomRecord* record, int64 ints);

		private int32 recordCapacity;
		private int32 poolLength;
		private int32 poolCapacity;
	}
}
