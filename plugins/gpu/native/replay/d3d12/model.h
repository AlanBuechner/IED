// model.h - a D3D12 capture as the replayer reads it, before any GPU work (docs/22 "Replay"): the objects and their
// creation records, the descriptor heaps' contents and resources' states at the frame's start, the initial contents,
// and the frame - its operations in order, each submitted list's commands, and the events (EIDs) the UI shows.
// Records are decoded with ModelMap: object references come back as ids, addresses and descriptor handles as tagged
// (id, offset) values, so the model needs no device.
#pragma once

#include "capfile.h"
#include "d3d12_records.h"

#include <map>

namespace fb::replay::d3d12
{
	using namespace fb::d3d12;

	// Tagged values: an address is (resource id << 40 | offset), a descriptor handle (heap id << 32 | index).
	inline u64 TagAddress(u64 id, u64 offset) { return id << 40 | (offset & ((1ull << 40) - 1)); }
	inline u64 AddressId(u64 tagged) { return tagged >> 40; }
	inline u64 AddressOffset(u64 tagged) { return tagged & ((1ull << 40) - 1); }
	inline u64 TagDescriptor(u64 heap, u32 index) { return heap << 32 | index; }
	inline u64 DescriptorHeapId(u64 tagged) { return tagged >> 32; }
	inline u32 DescriptorIndex(u64 tagged) { return (u32)tagged; }
	inline u64 IdOf(const void* object) { return (u64)(uintptr_t)object; }

	class Model;

	class ModelMap : public SerReadMap
	{
	public:
		explicit ModelMap(const Model& model) : model_(model) {}
		void* Object(u64 id) override { return (void*)(uintptr_t)id; }
		u64 Address(u64 id, u64 offset) override { return TagAddress(id, offset); }
		u64 CpuDescriptor(u64 heap, u32 index) override { return TagDescriptor(heap, index); }
		u64 GpuDescriptor(u64 heap, u32 index) override { return TagDescriptor(heap, index); }
		std::string Name(u64 id) override;

	private:
		const Model& model_;
	};

	struct ObjectEntry
	{
		u64 id = 0;
		ObjectKind kind = ObjectKind::Unknown;
		u32 recordType = 0;
		const u8* record = nullptr;
		u64 recordSize = 0;
		std::string name;
		bool inFrame = false;     // made during the captured frame
	};

	struct InitialData
	{
		u32 subresource;
		D3D12_SUBRESOURCE_FOOTPRINT footprint;
		const u8* data;
		u64 size;
	};

	struct CommandRef
	{
		Cmd command;
		const u8* args;
		u32 size;
		u32 eid;
	};

	struct ListRef
	{
		u64 list;
		std::vector<CommandRef> commands;
	};

	enum class OpKind : u8 { Submit, MapWrite, Signal, Wait, QueueMarker, Present, Create, Descriptor, CopyDescriptors, Destroyed, FenceSignal, Other };

	struct FrameOp
	{
		OpKind kind;
		u32 recordType;
		const u8* data;
		u64 size;
		u64 queue = 0;
		std::vector<ListRef> lists;
		u32 eid = 0;              // the event of a marker, present or fence operation; a submission's first command
	};

	enum class EventKind : u8 { Marker, Draw, Dispatch, Copy, Clear, Resolve, Barrier, State, Query, Present, Fence, Other };
	const char* EventKindName(EventKind kind);

	struct Event
	{
		u32 eid;
		i32 parent;               // index of the marker event it is inside, -1 at the top
		EventKind kind;
		u64 queue;
		u32 op;                   // its frame operation
		i32 list = -1;            // index in the operation's lists
		i32 command = -1;         // index in the list's commands
		bool hidden = false;      // EndEvent and state calls: shown only with "all calls"
		u32 lastEid = 0;          // a marker: the last event inside it
		std::string name;
	};

	class Model
	{
	public:
		// Reads every record; false with `error` for a capture it cannot use.
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
		// A command's arguments decoded into `args` (the command's *Args structure, at least CommandArgsSize bytes).
		bool DecodeCommand(const CommandRef& command, void* args, SerReadMap* map, JsonWriter* describe = nullptr) const;

		const ObjectEntry* Find(u64 id) const;
		std::string NameOf(u64 id) const;
		const Event* EventAt(u32 eid) const { return eid >= 1 && eid <= events.size() ? &events[eid - 1] : nullptr; }
		const CommandRef* CommandOf(const Event& event) const;
		// A short label of a command: "DrawIndexedInstanced(36, 1, 0, 0, 0)".
		std::string Label(const CommandRef& command) const;

		DeviceInfo device = {};
		std::string meta = "{}";
		std::vector<std::string> warnings;
		std::map<u64, ObjectEntry> objects;
		std::map<u64, std::map<u32, std::pair<const u8*, u64>>> descriptors;    // heap -> index -> its DescriptorWrite record
		std::map<u64, std::vector<D3D12_RESOURCE_STATES>> states;               // resource -> states at the frame's start
		std::map<u64, std::vector<InitialData>> initial;
		std::map<u64, u64> swapChainOf;                                          // buffer resource -> swap chain
		std::vector<FrameOp> frame;
		std::vector<Event> events;
		u32 thumbnailWidth = 0, thumbnailHeight = 0;
		const u8* thumbnail = nullptr;

	private:
		void BuildEvents();
		void AddCommandEvents(u32 opIndex, std::map<u64, std::vector<i32>>& stacks);

		Arena arena_;
	};
}
