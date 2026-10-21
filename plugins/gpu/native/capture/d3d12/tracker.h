// tracker.h - what the capture library knows about a wrapped D3D12 device's objects (docs/22 "Capture"): an id and the
// creation record of every object, the contents of every descriptor heap slot, buffers' GPU address ranges, and each
// resource's state as the submitted barriers left it. It is the capture side's SerWriteMap: object pointers, addresses
// and descriptor handles become ids as records are written.
#pragma once

#include "d3d12_records.h"

#include <map>
#include <shared_mutex>
#include <unordered_map>

namespace fb::capture::d3d12
{
	using namespace fb::d3d12;

	struct ResourceTrack
	{
		ResourceKind kind = ResourceKind::Committed;
		D3D12_RESOURCE_DESC1 desc = {};
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_CPU_PAGE_PROPERTY cpuPage = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		u64 gpuAddress = 0;
		u32 subresources = 1;
		bool usesLayouts = false;
		std::vector<D3D12_RESOURCE_STATES> states;    // per subresource
		std::vector<D3D12_BARRIER_LAYOUT> layouts;

		bool IsBuffer() const { return desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER; }
		// CPU-written memory whose contents a capture reads through Map (upload heaps and their kin).
		bool CpuWritable() const;
		bool Readback() const { return heapType == D3D12_HEAP_TYPE_READBACK || (heapType == D3D12_HEAP_TYPE_CUSTOM && cpuPage == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK && !IsBuffer()); }
		void SetState(u32 subresource, D3D12_RESOURCE_STATES state);
	};

	struct DescriptorHeapTrack
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		u64 cpuStart = 0;
		u64 gpuStart = 0;
		u32 increment = 0;
		std::vector<std::unique_ptr<DescriptorWrite[]>> pages;

		static constexpr u32 kPage = 1024;
		// The slot's view; null when `create` is false and nothing was ever written there.
		DescriptorWrite* Slot(u32 index, bool create);
	};

	struct Object
	{
		u64 id = 0;
		ObjectKind kind = ObjectKind::Unknown;
		void* real = nullptr;           // what the program holds (a proxy for queues and lists)
		ID3D12Object* named = nullptr;  // the runtime's object, which keeps the name SetName gave it
		bool alive = true;
		u32 recordType = 0;            // its creation record (0: none, e.g. allocators)
		std::vector<u8> record;
		std::unique_ptr<ResourceTrack> resource;
		std::unique_ptr<DescriptorHeapTrack> heap;
	};

	class Tracker : public SerWriteMap
	{
	public:
		explicit Tracker(u32 rtvIncrement, u32 dsvIncrement, u32 viewIncrement, u32 samplerIncrement);

		// A new object and its creation record (`serialize` writes the record's payload, ids already assigned).
		template <class Rec> Object* Add(void* real, ObjectKind kind, u32 recordType, Rec& record) {
			Object* object = Register(real, kind);
			if (recordType) {
				ByteWriter out;
				record.id = object->id;
				Ser s(out, this);
				Serialize(s, record);
				object->recordType = recordType;
				object->record = std::move(out.Buffer());
			}
			return object;
		}
		Object* Register(void* real, ObjectKind kind);
		Object* Find(const void* real);
		Object* FindId(u64 id);
		// Marks an object dead (its last reference went); true when it was known.
		bool Destroyed(u64 id);
		// Every object alive now, in creation order (ids grow with creation).
		std::vector<Object*> Live();

		// Buffers' GPU addresses, for addresses in commands and views.
		void AddAddressRange(Object* resource);
		// Descriptor heaps' handle ranges.
		void AddHeapRanges(Object* heap);
		u32 Increment(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

		// A view made into a CPU descriptor handle; the slot written (heap id, index), or {0, 0} for an unknown handle.
		std::pair<u64, u32> WriteDescriptor(SIZE_T handle, const DescriptorWrite& view);
		// CopyDescriptors(Simple): copies the shadow slots; calls `each` with every (dst heap, dst index, src heap, src
		// index, count) run it copied.
		void CopyDescriptors(SIZE_T dst, SIZE_T src, u32 count, D3D12_DESCRIPTOR_HEAP_TYPE type, std::vector<DescriptorCopy>* runs);

		// SerWriteMap
		u64 Id(const void* object) override;
		void Address(u64 va, u64& id, u64& offset) override;
		void CpuDescriptor(u64 ptr, u64& heap, u32& index) override;
		void GpuDescriptor(u64 ptr, u64& heap, u32& index) override;

		// Objects a command referenced that the library never saw made (created before the device was wrapped).
		u64 UnknownObjects() const { return unknown_; }

		std::shared_mutex lock;

	private:
		Object* HeapOfCpu(u64 ptr, u32& index);
		Object* HeapOfGpu(u64 ptr, u32& index);

		std::unordered_map<const void*, Object*> byReal_;
		std::map<u64, std::unique_ptr<Object>> byId_;
		std::vector<std::unique_ptr<Object>> graveyard_;
		std::map<u64, Object*> addresses_;    // start -> buffer
		std::map<u64, Object*> cpuHeaps_;     // cpu start -> heap
		std::map<u64, Object*> gpuHeaps_;     // gpu start -> shader-visible heap
		u64 nextId_ = 1;
		u64 unknown_ = 0;
		u32 increments_[4];
	};

	// Watches an object's lifetime: attached as private data, released by the runtime when the object is destroyed.
	void WatchLifetime(ID3D12Object* object, Tracker& tracker, u64 id);

	// The number of subresources of `desc` on this device (planes included).
	u32 SubresourceCount(ID3D12Device* device, const D3D12_RESOURCE_DESC1& desc);
	D3D12_RESOURCE_DESC ToDesc0(const D3D12_RESOURCE_DESC1& desc);
	D3D12_RESOURCE_DESC1 ToDesc1(const D3D12_RESOURCE_DESC& desc);
}
