// vkmodel.h - a Vulkan capture as the replayer reads it, before any GPU work (docs/22 "Replay"): the device, the objects
// and their creation records, the descriptor sets' contents, images' layouts and the initial contents at the frame's
// start, and the frame - its operations in order, each submitted command buffer's commands, and the events (EIDs) the
// UI shows. Records decode through ModelMap: handles come back as their ids, so the model needs no device.
#pragma once

#include "capfile.h"
#include "vk_records.h"

#include <map>

namespace fb::replay::vulkan
{
	using namespace fb::vk;

	template <class H> u64 IdOf(H handle) { return (u64)(uintptr_t)handle; }
	template <class H> H HandleOf(u64 id) { return (H)(uintptr_t)id; }

	class Model;

	class ModelMap : public SerReadMap
	{
	public:
		explicit ModelMap(const Model& model) : model_(model) {}
		void* Object(u64 id) override { return (void*)(uintptr_t)id; }
		std::string Name(u64 id) override;

	private:
		const Model& model_;
	};

	struct ObjectEntry
	{
		u64 id = 0;
		ObjectKind kind = ObjectKind::Unknown;
		u32 recordType = 0;
		const u8* record = nullptr;     // the latest record (a buffer or image bound during the frame is recorded again)
		u64 recordSize = 0;
		std::string name;
		bool inFrame = false;           // made during the captured frame
	};

	struct MemoryData
	{
		u64 memory;
		u64 offset;
		const u8* data;
		u64 size;
	};

	struct ImageData
	{
		u32 aspect;
		u32 mip;
		u32 layer;
		VkExtent3D extent;
		const u8* data;
		u64 size;
	};

	struct CommandRef
	{
		Cmd command;
		u16 flags;
		const u8* args;
		u32 size;
		u32 eid;
	};

	struct BufferRef
	{
		u64 commandBuffer;
		std::vector<CommandRef> commands;
	};

	enum class OpKind : u8 { Submit, MapWrite, Present, QueueLabel, Create, DescriptorUpdate, Destroyed, Other };

	struct FrameOp
	{
		OpKind kind;
		u32 recordType;
		const u8* data;
		u64 size;
		u64 queue = 0;
		std::vector<BufferRef> buffers;  // a submission's command buffers, batches in order
		u32 eid = 0;
	};

	enum class EventKind : u8 { Marker, Draw, Dispatch, Copy, Clear, Resolve, Barrier, State, Query, Present, Fence, Other };
	const char* EventKindName(EventKind kind);

	struct Event
	{
		u32 eid;
		i32 parent;                     // index of the marker event it is inside, -1 at the top
		EventKind kind;
		u64 queue;
		u32 op;
		i32 buffer = -1;                // index in the operation's buffers
		i32 command = -1;               // index in the buffer's commands
		bool hidden = false;            // label ends, state and barriers: shown only with "all calls"
		u32 lastEid = 0;                // a marker: the last event inside it
		std::string name;
	};

	class Model
	{
	public:
		bool Load(CaptureReader& file, std::string& error);

		template <class R> bool Decode(const u8* data, u64 size, R& out, SerReadMap* map, JsonWriter* describe = nullptr) const {
			ByteReader reader(data, (size_t)size);
			Ser s(reader, const_cast<Arena&>(arena_), map, describe);
			Serialize(s, out);
			return !s.Failed();
		}
		template <class R> bool DecodeModel(const u8* data, u64 size, R& out) const {
			ModelMap map(*this);
			return Decode(data, size, out, &map);
		}
		template <class R> bool DecodeObject(u64 id, R& out) const {
			const ObjectEntry* entry = Find(id);
			return entry && DecodeModel(entry->record, entry->recordSize, out);
		}
		bool DecodeCommand(const CommandRef& command, void* args, SerReadMap* map, JsonWriter* describe = nullptr) const;
		// Decoding into a caller's arena, which a replay resets as it goes (the model's own arena never shrinks).
		bool DecodeCommandIn(Arena& arena, const CommandRef& command, void* args, SerReadMap* map) const {
			ByteReader reader(command.args, command.size);
			Ser s(reader, arena, map);
			SerializeCommand(s, command.command, args);
			return !s.Failed();
		}
		template <class R> bool DecodeIn(Arena& arena, const u8* data, u64 size, R& out, SerReadMap* map) const {
			ByteReader reader(data, (size_t)size);
			Ser s(reader, arena, map);
			Serialize(s, out);
			return !s.Failed();
		}

		const ObjectEntry* Find(u64 id) const;
		std::string NameOf(u64 id) const;
		const Event* EventAt(u32 eid) const { return eid >= 1 && eid <= events.size() ? &events[eid - 1] : nullptr; }
		const CommandRef* CommandOf(const Event& event) const;
		// The command's Vulkan name ("vkCmdDrawIndexed") and a short label of it ("vkCmdDrawIndexed(36, 1, 0, 0, 0)").
		static std::string CommandName(const CommandRef& command);
		std::string Label(const CommandRef& command) const;

		DeviceInfo device = {};
		std::string meta = "{}";
		std::vector<std::string> warnings;
		std::map<u64, ObjectEntry> objects;
		std::vector<std::pair<const u8*, u64>> setupDescriptors;   // DescriptorUpdate records of the sets' contents at the start
		std::map<u64, std::vector<VkImageLayout>> layouts;         // image -> layer-major layouts at the frame's start
		std::vector<MemoryData> memoryContents;
		std::map<u64, std::pair<const u8*, u64>> bufferContents;
		std::map<u64, std::vector<ImageData>> imageContents;
		std::vector<FrameOp> frame;
		std::vector<Event> events;
		u32 thumbnailWidth = 0, thumbnailHeight = 0;
		const u8* thumbnail = nullptr;

	private:
		void ParseSubmit(FrameOp& op);
		void BuildEvents();

		Arena arena_;
	};
}
