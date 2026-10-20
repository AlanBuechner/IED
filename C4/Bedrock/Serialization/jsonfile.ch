// A JSON file decoded while it is still being read (roadmap 3): JsonFileStream feeds a JsonStream from an
// FS::ChunkReader, so its calls wait for the chunk being read instead of saying NeedMore. ReadNext decodes the next
// whole value into a value of any reflected type (reflectvalue.ch's rules), which is how a large map file's
// entities are loaded one at a time while the rest of the file is still on its way; ReadStreamed does the same over
// any JsonStream.
//     Serialization::JsonFileStream level = Serialization::JsonFileStream("level.json");   // {"entities": [...]}
//     level.Next(); level.Next(); level.Next();                                           // {, "entities", [
//     Entity entity;
//     while (level.ReadNext(entity) == Serialization::JsonEvent::Value) { Spawn(entity); entity = Entity(); }

include "jsonstream.ch"
include "reflectvalue.ch"
include "../FS/chunks.ch"

namespace Serialization
{
	class JsonFileStream
	{
		[operator("create")] public void Init() {}
		// Opens `path` and starts reading it; a file that cannot be opened makes every call Error.
		[operator("create")] public void Init(const char* path, int64 chunkBytes = FS::kDefaultChunkBytes);

		public bool IsOpen() const { return file.IsOpen(); }
		// JsonStream's calls, never NeedMore.
		public JsonEvent Next();
		public JsonEvent ReadValue(DomDoc& doc, DomNode& node);
		public JsonEvent SkipValue();
		// The next whole value read into `value`: Value, or the event that stands where a value would. A value that does
		// not fit T ends the stream with an Error.
		template<T> public JsonEvent ReadNext(T& value) {
			values.Clear();
			DomNode node;
			JsonEvent event = ReadValue(values, node);
			return event == JsonEvent::Value ? Fitted(Serialization::ReadValue(node, DescribeValue(nameof(T)), (void*)&value)) : event;
		}
		// Text, the values of the last event, Depth and Failure.
		public const JsonStream& Stream() const { return json; }

	private:
		private void Refill();   // feeds the next chunk, or finishes the stream at the end of the file
		private JsonEvent Fitted(bool fits);

		private FS::ChunkReader file;
		private JsonStream json;
		private DomDoc values;
	}

	// The next whole value of `json` read into `value`, `doc` holding it while it arrives (clear it after a Value): Value,
	// NeedMore, or the event that stands where a value would. A value that does not fit T ends the stream with an Error.
	template<T> JsonEvent ReadStreamed(JsonStream& json, DomDoc& doc, T& value) {
		DomNode node;
		JsonEvent event = json.ReadValue(doc, node);
		if (event != JsonEvent::Value || ReadValue(node, DescribeValue(nameof(T)), (void*)&value)) return event;
		json.Stop("a value does not fit the type it is read into");
		return JsonEvent::Error;
	}
}
