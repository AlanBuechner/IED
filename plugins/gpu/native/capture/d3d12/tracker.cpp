#include "tracker.h"

#include <atomic>
#include <mutex>

namespace fb::capture::d3d12
{
	// {6B0E1C8A-2F4D-4E77-9A3B-7C2D51F0B9A1}: the private data slot of the lifetime watcher.
	static const GUID kLifetimeGuid = {0x6b0e1c8a, 0x2f4d, 0x4e77, {0x9a, 0x3b, 0x7c, 0x2d, 0x51, 0xf0, 0xb9, 0xa1}};

	bool ResourceTrack::CpuWritable() const {
		if (!IsBuffer()) return false;
		if (heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_GPU_UPLOAD) return true;
		return heapType == D3D12_HEAP_TYPE_CUSTOM && (cpuPage == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE || cpuPage == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
	}

	void ResourceTrack::SetState(u32 subresource, D3D12_RESOURCE_STATES state) {
		if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
			states.assign(1, state);
			return;
		}
		if (states.size() == 1 && subresources > 1) states.assign(subresources, states[0]);
		if (subresource < states.size()) states[subresource] = state;
		// Back to one entry when every subresource agrees.
		bool same = true;
		for (D3D12_RESOURCE_STATES s : states) same = same && s == states[0];
		if (same && states.size() > 1) states.resize(1);
	}

	DescriptorWrite* DescriptorHeapTrack::Slot(u32 index, bool create) {
		if (index >= desc.NumDescriptors) return nullptr;
		const u32 page = index / kPage;
		if (page >= pages.size()) {
			if (!create) return nullptr;
			pages.resize(page + 1);
		}
		if (!pages[page]) {
			if (!create) return nullptr;
			pages[page].reset(new DescriptorWrite[kPage]());
		}
		return &pages[page][index % kPage];
	}

	Tracker::Tracker(u32 rtvIncrement, u32 dsvIncrement, u32 viewIncrement, u32 samplerIncrement) {
		increments_[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = viewIncrement;
		increments_[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = samplerIncrement;
		increments_[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = rtvIncrement;
		increments_[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = dsvIncrement;
	}

	u32 Tracker::Increment(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return type < 4 ? increments_[type] : 0; }

	Object* Tracker::Register(void* real, ObjectKind kind) {
		std::unique_lock<std::shared_mutex> guard(lock);
		auto object = std::make_unique<Object>();
		object->id = nextId_++;
		object->kind = kind;
		object->real = real;
		Object* raw = object.get();
		// A pointer the runtime reuses for a new object replaces the dead one's entry.
		byReal_[real] = raw;
		byId_[raw->id] = std::move(object);
		return raw;
	}

	Object* Tracker::Find(const void* real) {
		std::shared_lock<std::shared_mutex> guard(lock);
		auto found = byReal_.find(real);
		return found == byReal_.end() ? nullptr : found->second;
	}

	Object* Tracker::FindId(u64 id) {
		std::shared_lock<std::shared_mutex> guard(lock);
		auto found = byId_.find(id);
		return found == byId_.end() ? nullptr : found->second.get();
	}

	bool Tracker::Destroyed(u64 id) {
		std::unique_lock<std::shared_mutex> guard(lock);
		auto found = byId_.find(id);
		if (found == byId_.end()) return false;
		Object* object = found->second.get();
		object->alive = false;
		auto real = byReal_.find(object->real);
		if (real != byReal_.end() && real->second == object) byReal_.erase(real);
		if (object->resource && object->resource->gpuAddress) {
			auto range = addresses_.find(object->resource->gpuAddress);
			if (range != addresses_.end() && range->second == object) addresses_.erase(range);
		}
		if (object->heap) {
			auto cpu = cpuHeaps_.find(object->heap->cpuStart);
			if (cpu != cpuHeaps_.end() && cpu->second == object) cpuHeaps_.erase(cpu);
			auto gpu = gpuHeaps_.find(object->heap->gpuStart);
			if (gpu != gpuHeaps_.end() && gpu->second == object) gpuHeaps_.erase(gpu);
		}
		// Root signatures stay: pipeline states and command signatures made from them keep using them. Other dead objects
		// keep only their small record of what they were, so a pointer another thread still holds stays valid.
		if (object->kind != ObjectKind::RootSignature) {
			object->heap.reset();
			object->record.clear();
			object->record.shrink_to_fit();
			graveyard_.push_back(std::move(found->second));
			byId_.erase(found);
		}
		return true;
	}

	std::vector<Object*> Tracker::Live() {
		std::shared_lock<std::shared_mutex> guard(lock);
		std::vector<Object*> live;
		live.reserve(byId_.size());
		for (auto& entry : byId_) live.push_back(entry.second.get());
		return live;
	}

	void Tracker::AddAddressRange(Object* resource) {
		std::unique_lock<std::shared_mutex> guard(lock);
		if (resource->resource && resource->resource->gpuAddress) addresses_[resource->resource->gpuAddress] = resource;
	}

	void Tracker::AddHeapRanges(Object* heap) {
		std::unique_lock<std::shared_mutex> guard(lock);
		cpuHeaps_[heap->heap->cpuStart] = heap;
		if (heap->heap->gpuStart) gpuHeaps_[heap->heap->gpuStart] = heap;
	}

	u64 Tracker::Id(const void* object) {
		if (!object) return 0;
		std::shared_lock<std::shared_mutex> guard(lock);
		auto found = byReal_.find(object);
		if (found != byReal_.end()) return found->second->id;
		++unknown_;
		return 0;
	}

	void Tracker::Address(u64 va, u64& id, u64& offset) {
		std::shared_lock<std::shared_mutex> guard(lock);
		auto after = addresses_.upper_bound(va);
		if (after != addresses_.begin()) {
			--after;
			const Object* buffer = after->second;
			const u64 start = after->first;
			if (va >= start && va - start < buffer->resource->desc.Width) {
				id = buffer->id;
				offset = va - start;
				return;
			}
		}
		id = 0;
		offset = va;
	}

	Object* Tracker::HeapOfCpu(u64 ptr, u32& index) {
		auto after = cpuHeaps_.upper_bound(ptr);
		if (after == cpuHeaps_.begin()) return nullptr;
		--after;
		DescriptorHeapTrack* heap = after->second->heap.get();
		if (!heap->increment) return nullptr;
		const u64 delta = ptr - heap->cpuStart;
		if (delta % heap->increment || delta / heap->increment >= heap->desc.NumDescriptors) return nullptr;
		index = (u32)(delta / heap->increment);
		return after->second;
	}

	Object* Tracker::HeapOfGpu(u64 ptr, u32& index) {
		auto after = gpuHeaps_.upper_bound(ptr);
		if (after == gpuHeaps_.begin()) return nullptr;
		--after;
		DescriptorHeapTrack* heap = after->second->heap.get();
		if (!heap->increment) return nullptr;
		const u64 delta = ptr - heap->gpuStart;
		if (delta % heap->increment || delta / heap->increment >= heap->desc.NumDescriptors) return nullptr;
		index = (u32)(delta / heap->increment);
		return after->second;
	}

	void Tracker::CpuDescriptor(u64 ptr, u64& heap, u32& index) {
		std::shared_lock<std::shared_mutex> guard(lock);
		Object* found = HeapOfCpu(ptr, index);
		heap = found ? found->id : 0;
		if (!found) index = 0;
	}

	void Tracker::GpuDescriptor(u64 ptr, u64& heap, u32& index) {
		std::shared_lock<std::shared_mutex> guard(lock);
		Object* found = HeapOfGpu(ptr, index);
		heap = found ? found->id : 0;
		if (!found) index = 0;
	}

	std::pair<u64, u32> Tracker::WriteDescriptor(SIZE_T handle, const DescriptorWrite& view) {
		std::unique_lock<std::shared_mutex> guard(lock);
		u32 index = 0;
		Object* heap = HeapOfCpu(handle, index);
		if (!heap) return {0, 0};
		DescriptorWrite* slot = heap->heap->Slot(index, true);
		if (!slot) return {0, 0};
		*slot = view;
		slot->heap = heap->id;
		slot->index = index;
		return {heap->id, index};
	}

	void Tracker::CopyDescriptors(SIZE_T dst, SIZE_T src, u32 count, D3D12_DESCRIPTOR_HEAP_TYPE type, std::vector<DescriptorCopy>* runs) {
		std::unique_lock<std::shared_mutex> guard(lock);
		const u32 step = Increment(type);
		u32 done = 0;
		while (done < count) {
			u32 dstIndex = 0, srcIndex = 0;
			Object* dstHeap = HeapOfCpu(dst + (u64)done * step, dstIndex);
			Object* srcHeap = HeapOfCpu(src + (u64)done * step, srcIndex);
			if (!dstHeap) return;
			u32 run = count - done;
			run = std::min(run, dstHeap->heap->desc.NumDescriptors - dstIndex);
			if (srcHeap) run = std::min(run, srcHeap->heap->desc.NumDescriptors - srcIndex);
			for (u32 i = 0; i < run; ++i) {
				DescriptorWrite* from = srcHeap ? srcHeap->heap->Slot(srcIndex + i, false) : nullptr;
				DescriptorWrite* to = dstHeap->heap->Slot(dstIndex + i, from != nullptr);
				if (!to) continue;
				if (from) *to = *from;
				else *to = DescriptorWrite();
				to->heap = dstHeap->id;
				to->index = dstIndex + i;
			}
			if (runs) runs->push_back({dstHeap->id, dstIndex, srcHeap ? srcHeap->id : 0, srcIndex, run});
			done += run;
		}
	}

	namespace
	{
		class LifetimeWatcher final : public IUnknown
		{
		public:
			LifetimeWatcher(Tracker& tracker, u64 id) : tracker_(tracker), id_(id) {}
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (riid == __uuidof(IUnknown)) {
					*out = this;
					AddRef();
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}
			ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
			ULONG STDMETHODCALLTYPE Release() override {
				const ULONG left = --references_;
				if (left == 0) {
					tracker_.Destroyed(id_);
					delete this;
				}
				return left;
			}

		private:
			Tracker& tracker_;
			u64 id_;
			std::atomic<ULONG> references_{1};
		};
	}

	void WatchLifetime(ID3D12Object* object, Tracker& tracker, u64 id) {
		LifetimeWatcher* watcher = new LifetimeWatcher(tracker, id);
		object->SetPrivateDataInterface(kLifetimeGuid, watcher);
		watcher->Release();
	}

	D3D12_RESOURCE_DESC ToDesc0(const D3D12_RESOURCE_DESC1& d) {
		D3D12_RESOURCE_DESC desc = {d.Dimension, d.Alignment, d.Width, d.Height, d.DepthOrArraySize, d.MipLevels, d.Format, d.SampleDesc, d.Layout, d.Flags};
		return desc;
	}

	D3D12_RESOURCE_DESC1 ToDesc1(const D3D12_RESOURCE_DESC& d) {
		D3D12_RESOURCE_DESC1 desc = {};
		desc.Dimension = d.Dimension;
		desc.Alignment = d.Alignment;
		desc.Width = d.Width;
		desc.Height = d.Height;
		desc.DepthOrArraySize = d.DepthOrArraySize;
		desc.MipLevels = d.MipLevels;
		desc.Format = d.Format;
		desc.SampleDesc = d.SampleDesc;
		desc.Layout = d.Layout;
		desc.Flags = d.Flags;
		return desc;
	}

	u32 SubresourceCount(ID3D12Device* device, const D3D12_RESOURCE_DESC1& desc) {
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return 1;
		D3D12_FEATURE_DATA_FORMAT_INFO info = {desc.Format, 1};
		if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info))) || info.PlaneCount == 0) info.PlaneCount = 1;
		u32 mips = desc.MipLevels;
		if (mips == 0) {
			u64 largest = std::max<u64>(desc.Width, desc.Height);
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) largest = std::max<u64>(largest, desc.DepthOrArraySize);
			mips = 1;
			while (largest > 1) {
				largest >>= 1;
				++mips;
			}
		}
		const u32 slices = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
		return mips * slices * info.PlaneCount;
	}
}
