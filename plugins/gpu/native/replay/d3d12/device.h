// device.h - the D3D12 replay device (docs/22 "Replay"): every object of a capture made again on a device of the
// capture's GPU, the frame's initial contents restored on demand, and the frame replayed up to any event - each
// submission's lists rebuilt from their commands with ids, addresses and descriptor handles mapped to the replay's
// own. Hooks let a caller add work around commands (timestamps, statistics, copies).
#pragma once

#include "model.h"
#include "replay/replay.h"

#include <unordered_map>

namespace fb::replay::d3d12
{
	// Work a caller adds to a replay: around each command, and at the end of each list before it closes.
	class ReplayHooks
	{
	public:
		virtual ~ReplayHooks() = default;
		virtual void Before(ID3D12GraphicsCommandList* list, const CommandRef& command) {}
		virtual void After(ID3D12GraphicsCommandList* list, const CommandRef& command) {}
		virtual void EndList(ID3D12GraphicsCommandList* list, D3D12_COMMAND_LIST_TYPE type) {}
	};

	class ReplayDevice : public SerReadMap
	{
	public:
		explicit ReplayDevice(const Model& model) : model_(model) {}
		~ReplayDevice();

		// Makes the device and every object; throws Failure.
		void Create(const JsonValue& options);
		// Replays the frame from its start through event `eid` (0: only the start), resources ending in their states
		// at that event. The work of the event itself is included.
		void ReplayTo(u32 eid, ReplayHooks* hooks = nullptr);

		ID3D12Device* Device() const { return device_; }
		ID3D12Resource* Resource(u64 id) const;
		D3D12_RESOURCE_STATES StateOf(u64 id, u32 subresource) const;
		// A list on the direct queue for the caller's own copies: Begin, record, then Run (executes and waits).
		ID3D12GraphicsCommandList* BeginUtility();
		void RunUtility();
		u64 TimestampFrequency(u64 queue) const;
		bool QueueSupportsTimestamps(u64 queue) const;
		D3D12_COMMAND_LIST_TYPE QueueType(u64 queue) const;

		std::string adapterName;
		std::string driver;
		std::vector<std::string> warnings;
		// Debug layer messages of the last replay: {eid, severity, text}.
		struct Message
		{
			u32 eid;
			std::string severity;
			std::string text;
		};
		std::vector<Message> messages;

		// SerReadMap
		void* Object(u64 id) override;
		u64 Address(u64 id, u64 offset) override;
		u64 CpuDescriptor(u64 heap, u32 index) override;
		u64 GpuDescriptor(u64 heap, u32 index) override;
		std::string Name(u64 id) override { return model_.NameOf(id); }

	private:
		struct HeapInfo
		{
			ID3D12DescriptorHeap* heap = nullptr;
			u64 cpu = 0;
			u64 gpu = 0;
			u32 increment = 0;
		};
		struct QueueInfo
		{
			ID3D12CommandQueue* queue = nullptr;
			D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		};
		struct ListSet
		{
			ID3D12CommandAllocator* allocator = nullptr;
			ID3D12GraphicsCommandList* list = nullptr;
		};
		struct Pristine
		{
			u64 id;
			u32 subresource;
			u64 offset;      // in the pristine buffer
			D3D12_SUBRESOURCE_FOOTPRINT footprint;
		};

		void PickAdapter(const JsonValue& options);
		void CreateObject(const ObjectEntry& entry);
		void CreateResource(const ObjectEntry& entry);
		void CreatePipeline(const ObjectEntry& entry);
		void WriteDescriptor(const u8* data, u64 size);
		void CopyDescriptors(const u8* data, u64 size);
		void PrepareInitialContents();
		void Reset();
		void RunSubmit(const FrameOp& op, u32 stopEid, ReplayHooks* hooks);
		void RunList(u64 queue, const ListRef& list, u32 stopEid, ReplayHooks* hooks);
		void RunCommand(ID3D12GraphicsCommandList* list, const CommandRef& command, std::vector<u8>& args);
		void Execute(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list);
		void WaitQueue(ID3D12CommandQueue* queue);
		void CollectMessages(u32 eid);
		ListSet& ListsFor(D3D12_COMMAND_LIST_TYPE type);
		const QueueInfo* QueueOf(u64 id) const;

		const Model& model_;
		Arena scratch_;   // what a replay decodes, reset for each command
		IDXGIAdapter1* adapter_ = nullptr;
		ID3D12Device* device_ = nullptr;
		ID3D12Device2* device2_ = nullptr;
		ID3D12InfoQueue* infoQueue_ = nullptr;
		ID3D12Fence* fence_ = nullptr;
		u64 fenceValue_ = 0;
		void* event_ = nullptr;
		std::unordered_map<u64, IUnknown*> objects_;
		std::unordered_map<const void*, u64> ids_;                             // resources, by the replay's object
		std::unordered_map<u64, u64> addresses_;
		std::unordered_map<u64, HeapInfo> heaps_;
		std::unordered_map<u64, QueueInfo> queues_;
		ID3D12CommandQueue* utilityQueue_ = nullptr;
		std::unordered_map<int, ListSet> lists_;
		ListSet utility_;
		std::unordered_map<u64, u8*> mapped_;                                   // CPU-visible buffers, mapped once
		std::unordered_map<u64, std::vector<D3D12_RESOURCE_STATES>> states_;    // as the last replay left them
		std::unordered_map<u64, u32> subresources_;
		ID3D12Resource* pristine_ = nullptr;
		u8* pristineMapped_ = nullptr;
		std::vector<Pristine> pristineItems_;
		bool descriptorsDirty_ = true;
		bool frameWritesDescriptors_ = false;
	};
}
