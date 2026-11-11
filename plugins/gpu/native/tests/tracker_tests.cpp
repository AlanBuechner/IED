// The capture library's shadow of descriptor heaps (capture/d3d12/tracker.cpp): a slot whose resource died is stale,
// even once the runtime hands the dead resource's pointer to a new one, and a frame's capture leaves it out.
#include "testing.h"

#include "capture/d3d12/tracker.h"

using namespace fb;
using namespace fb::capture::d3d12;

namespace
{
	constexpr u64 kHeapStart = 0x10000;
	constexpr u32 kIncrement = 32;

	// A heap of 8 views at a made-up CPU address; `real` stands in for the runtime's object.
	Object* AddHeap(Tracker& tracker, void* real) {
		Object* heap = tracker.Register(real, ObjectKind::DescriptorHeap);
		heap->heap = std::make_unique<DescriptorHeapTrack>();
		heap->heap->desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap->heap->desc.NumDescriptors = 8;
		heap->heap->cpuStart = kHeapStart;
		heap->heap->increment = kIncrement;
		tracker.AddHeapRanges(heap);
		return heap;
	}

	DescriptorWrite BufferView(void* resource) {
		DescriptorWrite write = {};
		write.kind = ViewKind::Srv;
		write.resource = (ID3D12Resource*)resource;
		write.hasDesc = 1;
		write.srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		return write;
	}

	SIZE_T Handle(u32 index) { return (SIZE_T)(kHeapStart + (u64)index * kIncrement); }
}

TEST(TrackerDropsViewsOfDeadResources) {
	Tracker tracker(kIncrement, kIncrement, kIncrement, kIncrement);
	int heapObject = 0, buffer = 0;   // only their addresses matter
	Object* heap = AddHeap(tracker, &heapObject);
	const u64 first = tracker.Register(&buffer, ObjectKind::Resource)->id;
	tracker.WriteDescriptor(Handle(2), BufferView(&buffer));
	const TrackedView& slot = *heap->heap->Slot(2, false);
	CHECK(slot.resource == first && tracker.IsCurrentLocked(slot));
	tracker.Destroyed(first);
	CHECK(!tracker.IsCurrentLocked(slot));
	// The runtime gives the dead buffer's address to a new one: the old view is still not the new buffer's.
	tracker.Register(&buffer, ObjectKind::Resource);
	CHECK(!tracker.IsCurrentLocked(slot));
	tracker.WriteDescriptor(Handle(2), BufferView(&buffer));
	CHECK(tracker.IsCurrentLocked(slot));
	// A null view names nothing and stays current.
	tracker.WriteDescriptor(Handle(3), BufferView(nullptr));
	CHECK(tracker.IsCurrentLocked(*heap->heap->Slot(3, false)));
}

TEST(TrackerCopiesTheResourcesAViewNamed) {
	Tracker tracker(kIncrement, kIncrement, kIncrement, kIncrement);
	int heapObject = 0, buffer = 0;
	Object* heap = AddHeap(tracker, &heapObject);
	const u64 id = tracker.Register(&buffer, ObjectKind::Resource)->id;
	tracker.WriteDescriptor(Handle(1), BufferView(&buffer));
	tracker.CopyDescriptors(Handle(5), Handle(1), 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nullptr);
	const TrackedView& copy = *heap->heap->Slot(5, false);
	CHECK(copy.resource == id && copy.view.index == 5 && tracker.IsCurrentLocked(copy));
	tracker.Destroyed(id);
	tracker.Register(&buffer, ObjectKind::Resource);
	CHECK(!tracker.IsCurrentLocked(copy));
}
