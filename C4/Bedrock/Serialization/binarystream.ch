// BinaryStream: records WriteBinary wrote, read while they are still arriving (roadmap 3). Fed with chunks of bytes,
// Read takes the next whole record and says NeedMore when the bytes so far end inside one, so a large binary map
// file is decoded as it is read (FS::ChunkReader) and a socket's records as they come.
//     Serialization::BinaryStream records;
//     records.Feed(chunk);
//     Entity entity;
//     while (records.Read(nameof(Entity), &entity) == Serialization::BinaryRead::Record) Spawn(entity);

include "binary.ch"

namespace Serialization
{
	enum class BinaryRead {
		Record,     // a whole record was read
		NeedMore,   // the bytes so far end inside the next record: Feed or Finish and ask again
		End,        // finished, with no bytes left
		Error       // the type is not reflected, or the bytes ended inside a record
	}

	class BinaryStream
	{
		[operator("create")] public void Init() {}

		public void Feed(Core::StringView bytes);
		// No more bytes will come.
		public void Finish() { finished = true; }

		// The next record of `type` into `destination`, which a NeedMore may have partly written.
		public BinaryRead Read(Reflection::Type type, void* destination);
		public BinaryRead Read(const char* typeName, void* destination) { return Read(Reflection::FindType(typeName), destination); }

		// Bytes fed and not yet read.
		public int64 Pending() const { return held.ByteLength() - position; }

	private:
		private Core::StringBuilder held;
		private Core::StringBuilder spare;
		private int64 position;
		private bool finished;
	}
}
