#include "framecapture.h"
#include "wrappers.h"

#include "formats.h"
#include "png.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

namespace fb::capture::d3d12
{
	// Readback memory per batch of initial-contents copies (a larger resource gets a batch of its own size).
	static constexpr u64 kCopyBatch = 256ull * 1024 * 1024;
	// Mapped memory is compared with its shadow in blocks of this size.
	static constexpr size_t kDiffBlock = 256;

	FrameCapture::~FrameCapture() {
		if (list_) list_->Release();
		if (allocator_) allocator_->Release();
		if (queue_) queue_->Release();
		if (fence_) fence_->Release();
		if (event_) CloseHandle((HANDLE)event_);
	}

	SerWriteMap* FrameCapture::MapForRecords() { return &device_.Objects(); }

	void FrameCapture::Warn(const std::string& text) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		for (const std::string& known : warnings_)
			if (known == text) return;
		warnings_.push_back(text);
		Log("capture warning: %s", text.c_str());
	}

	bool FrameCapture::MakeObjects() {
		if (list_) return true;
		ID3D12Device* device = device_.Real();
		D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
		if (FAILED(device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&allocator_))) return false;
		if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
		if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
		queue_->SetName(L"Flashbang capture queue");
		event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		return true;
	}

	void FrameCapture::WaitIdle() {
		std::vector<ID3D12CommandQueue*> queues;
		for (WrappedQueue* queue : device_.Queues()) queues.push_back(queue->Real());
		queues.push_back(queue_);
		for (ID3D12CommandQueue* queue : queues) {
			const u64 value = ++fenceValue_;
			if (FAILED(queue->Signal(fence_, value))) continue;
			if (fence_->GetCompletedValue() < value) {
				fence_->SetEventOnCompletion(value, (HANDLE)event_);
				WaitForSingleObject((HANDLE)event_, 30000);
			}
		}
	}

	void FrameCapture::Execute() {
		list_->Close();
		ID3D12CommandList* lists[] = {list_};
		queue_->ExecuteCommandLists(1, lists);
		const u64 value = ++fenceValue_;
		queue_->Signal(fence_, value);
		if (fence_->GetCompletedValue() < value) {
			fence_->SetEventOnCompletion(value, (HANDLE)event_);
			WaitForSingleObject((HANDLE)event_, 60000);
		}
		allocator_->Reset();
		list_->Reset(allocator_, nullptr);
	}

	// --- Frame boundaries ------------------------------------------------------------------------------------------

	void FrameCapture::AfterPresent() {
		const u64 frame = Session::Get().NextFrame();
		if (!Session::Get().TakeTrigger()) return;
		std::lock_guard<std::recursive_mutex> guard(lock_);
		frame_ = frame + 1;
		if (!Begin()) Session::Get().Failed("the capture could not start (see the program's debug output)");
	}

	void FrameCapture::BeforePresent(WrappedSwapChain* swapChain, UINT syncInterval, UINT flags) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (active_) End(swapChain, syncInterval, flags);
	}

	void FrameCapture::ManualStart(u64 frame) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (active_) return;
		frame_ = frame;
		if (!Begin()) Session::Get().Failed("the capture could not start (see the program's debug output)");
	}

	void FrameCapture::ManualEnd() {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (active_) End(nullptr, 0, 0);
	}

	bool FrameCapture::Begin() {
		if (!MakeObjects()) {
			Log("cannot make the capture's own queue and list");
			return false;
		}
		base_ = Session::Get().NewCaptureBase(frame_);
		std::string error;
		if (!file_.Open(base_ + L".flash", Api::D3D12, error)) {
			Session::Get().Failed(error);
			return false;
		}
		warnings_.clear();
		stream_.Clear();
		shadows_.clear();
		events_ = 0;
		WaitIdle();
		WriteSetup();
		WriteInitialContents();
		file_.Record(kRecordFrameBegin, nullptr, 0);
		active_ = true;
		Session::Get().SetCapturing(true);
		Log("capturing frame %llu", (unsigned long long)frame_);
		return true;
	}

	// --- The frame's start: objects, descriptors, states ------------------------------------------------------------

	static void AdapterInfo(ID3D12Device* device, DeviceInfo& info) {
		const LUID luid = device->GetAdapterLuid();
		info.luid = ((u64)(u32)luid.HighPart << 32) | luid.LowPart;
		IDXGIFactory4* factory = nullptr;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) return;
		IDXGIAdapter1* adapter = nullptr;
		if (SUCCEEDED(factory->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), (void**)&adapter))) {
			DXGI_ADAPTER_DESC1 desc;
			if (SUCCEEDED(adapter->GetDesc1(&desc))) {
				info.adapter = Narrow(desc.Description);
				info.vendorId = desc.VendorId;
				info.deviceId = desc.DeviceId;
				info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
			}
			LARGE_INTEGER umd;
			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
				const u64 v = (u64)umd.QuadPart;
				info.driver = Format("%u.%u.%u.%u", (u32)(v >> 48), (u32)((v >> 32) & 0xFFFF), (u32)((v >> 16) & 0xFFFF), (u32)(v & 0xFFFF));
			}
			adapter->Release();
		}
		factory->Release();
	}

	static std::string ObjectNameOf(ID3D12Object* object) {
		if (!object) return std::string();
		UINT size = 0;
		if (FAILED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, nullptr)) || size < 2) return std::string();
		std::wstring name(size / 2, L'\0');
		if (FAILED(object->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, name.data()))) return std::string();
		while (!name.empty() && name.back() == L'\0') name.pop_back();
		return Narrow(name);
	}

	void FrameCapture::WriteSetup() {
		Tracker& tracker = device_.Objects();
		ID3D12Device* device = device_.Real();
		DeviceInfo info = {};
		info.id = 0;
		AdapterInfo(device, info);
		D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
		const D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
		levels.NumFeatureLevels = 5;
		levels.pFeatureLevelsRequested = wanted;
		info.featureLevel = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))) ? levels.MaxSupportedFeatureLevel : D3D_FEATURE_LEVEL_11_0;
		info.rtvIncrement = tracker.Increment(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		info.dsvIncrement = tracker.Increment(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		info.viewIncrement = tracker.Increment(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		info.samplerIncrement = tracker.Increment(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		ID3D12DebugDevice* debug = nullptr;
		info.debugLayer = SUCCEEDED(device->QueryInterface(__uuidof(ID3D12DebugDevice), (void**)&debug)) ? 1 : 0;
		if (debug) debug->Release();
		FileRecord(kDevice, info);

		std::vector<Object*> live = tracker.Live();
		// Creation records in creation order (what a record names was made before it). Fences are written with their
		// value now, the value their waits and signals start from.
		{
			std::shared_lock<std::shared_mutex> guard(tracker.lock);
			for (Object* object : live) {
				if (object->kind == ObjectKind::Fence && object->alive) {
					FenceCreate fence = {object->id, ((ID3D12Fence*)object->real)->GetCompletedValue(), D3D12_FENCE_FLAG_NONE};
					ByteWriter payload;
					Ser s(payload, &tracker);
					Serialize(s, fence);
					file_.Record(kCreateFence, payload);
					continue;
				}
				if (object->recordType && !object->record.empty()) file_.Record(object->recordType, object->record.data(), object->record.size());
			}
		}
		for (Object* object : live) {
			if (!object->alive) continue;
			ObjectName name = {object->id, ObjectNameOf(object->named)};
			if (!name.name.empty()) FileRecord(kName, name);
		}
		// Every written descriptor of every heap, and each resource's state.
		std::shared_lock<std::shared_mutex> guard(tracker.lock);
		for (Object* object : live) {
			if (!object->alive) continue;
			if (object->heap) {
				for (size_t page = 0; page < object->heap->pages.size(); ++page) {
					if (!object->heap->pages[page]) continue;
					for (u32 i = 0; i < DescriptorHeapTrack::kPage; ++i) {
						DescriptorWrite& slot = object->heap->pages[page][i];
						if (slot.kind == ViewKind::None) continue;
						ByteWriter payload;
						Ser s(payload, &tracker);
						Serialize(s, slot);
						file_.Record(kDescriptor, payload);
					}
				}
			}
			if (object->resource) {
				ResourceState state = {object->id, (u32)object->resource->states.size(), object->resource->states.data(), 0, nullptr};
				if (object->resource->usesLayouts) {
					state.layoutCount = (u32)object->resource->layouts.size();
					state.layouts = object->resource->layouts.data();
				}
				ByteWriter payload;
				Ser s(payload, &tracker);
				Serialize(s, state);
				file_.Record(kResourceState, payload);
			}
		}
		if (tracker.UnknownObjects()) Warn("commands referenced objects made before the device was handed to the capture library");
	}

	// --- Initial contents -------------------------------------------------------------------------------------------

	void FrameCapture::WriteInitialContents() {
		std::vector<Object*> live;
		for (Object* object : device_.Objects().Live())
			if (object->alive && object->resource) live.push_back(object);
		ReadUploads(live);
		CopyResources(live);
	}

	void FrameCapture::ReadUploads(const std::vector<Object*>& live) {
		for (Object* object : live) {
			ResourceTrack& track = *object->resource;
			if (!track.CpuWritable()) continue;
			ID3D12Resource* resource = (ID3D12Resource*)object->real;
			void* mapped = nullptr;
			const D3D12_RANGE read = {0, (SIZE_T)track.desc.Width};
			if (FAILED(resource->Map(0, &read, &mapped))) {
				Warn(Format("could not map buffer %llu to read its contents", (unsigned long long)object->id));
				continue;
			}
			Shadow shadow = {object->id, resource, std::vector<u8>((size_t)track.desc.Width)};
			memcpy(shadow.bytes.data(), mapped, shadow.bytes.size());
			const D3D12_RANGE none = {0, 0};
			resource->Unmap(0, &none);
			InitialContents contents = {};
			contents.id = object->id;
			contents.footprint = {DXGI_FORMAT_UNKNOWN, (UINT)track.desc.Width, 1, 1, (UINT)track.desc.Width};
			contents.data = shadow.bytes.data();
			contents.size = shadow.bytes.size();
			FileRecord(kInitialContents, contents);
			shadows_.push_back(std::move(shadow));
		}
	}

	namespace
	{
		struct CopyItem
		{
			Object* object;
			u32 subresource;
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
			u64 bytes;
		};

		// The state a copy needs: none when the tracked state already allows reading as a copy source (or a buffer in
		// COMMON, which promotes for the copy and decays after it).
		bool NeedsTransition(const ResourceTrack& track, D3D12_RESOURCE_STATES state) {
			if (state & D3D12_RESOURCE_STATE_COPY_SOURCE) return false;
			if (state == D3D12_RESOURCE_STATE_COMMON && (track.IsBuffer() || (track.desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS))) return false;
			return true;
		}
	}

	void FrameCapture::CopyResources(const std::vector<Object*>& live) {
		ID3D12Device* device = device_.Real();
		std::vector<CopyItem> items;
		for (Object* object : live) {
			ResourceTrack& track = *object->resource;
			if (track.CpuWritable() || track.Readback()) continue;
			if (track.kind == ResourceKind::Reserved) {
				Warn("reserved (tiled) resources are not captured yet; their contents start empty on replay");
				continue;
			}
			if (track.desc.SampleDesc.Count > 1) {
				Warn("multisampled textures' contents are not captured yet; they start empty on replay");
				continue;
			}
			if (track.IsBuffer()) {
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {0, {DXGI_FORMAT_UNKNOWN, (UINT)track.desc.Width, 1, 1, (UINT)track.desc.Width}};
				items.push_back({object, 0, footprint, track.desc.Width});
				continue;
			}
			const D3D12_RESOURCE_DESC desc = ToDesc0(track.desc);
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(track.subresources);
			std::vector<UINT> rows(track.subresources);
			std::vector<UINT64> rowBytes(track.subresources);
			UINT64 total = 0;
			device->GetCopyableFootprints(&desc, 0, track.subresources, 0, footprints.data(), rows.data(), rowBytes.data(), &total);
			for (u32 sub = 0; sub < track.subresources; ++sub) {
				const u64 bytes = (u64)footprints[sub].Footprint.RowPitch * rows[sub] * footprints[sub].Footprint.Depth;
				items.push_back({object, sub, footprints[sub], bytes});
			}
		}
		size_t at = 0;
		while (at < items.size()) {
			// One batch: whole resources while they fit (a resource larger than a batch is a batch of its own).
			u64 used = 0;
			size_t end = at;
			while (end < items.size()) {
				Object* object = items[end].object;
				u64 resourceBytes = 0;
				size_t next = end;
				while (next < items.size() && items[next].object == object) {
					resourceBytes += AlignUp(items[next].bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
					++next;
				}
				if (used && used + resourceBytes > kCopyBatch) break;
				used += resourceBytes;
				end = next;
			}
			D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_READBACK};
			D3D12_RESOURCE_DESC bufferDesc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, std::max<u64>(used, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
			ID3D12Resource* readback = nullptr;
			if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&readback))) {
				Warn(Format("not enough memory to read %llu MB of resource contents", (unsigned long long)(used >> 20)));
				at = end;
				continue;
			}
			std::vector<u64> offsets;
			std::vector<D3D12_RESOURCE_BARRIER> before, after;
			u64 offset = 0;
			for (size_t i = at; i < end; ++i) {
				CopyItem& item = items[i];
				ResourceTrack& track = *item.object->resource;
				ID3D12Resource* resource = (ID3D12Resource*)item.object->real;
				const D3D12_RESOURCE_STATES state = track.states.size() > item.subresource ? track.states[item.subresource] : track.states.empty() ? D3D12_RESOURCE_STATE_COMMON : track.states[0];
				if (NeedsTransition(track, state)) {
					D3D12_RESOURCE_BARRIER barrier = {};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Transition.pResource = resource;
					barrier.Transition.Subresource = track.IsBuffer() ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : item.subresource;
					barrier.Transition.StateBefore = state;
					barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
					before.push_back(barrier);
					std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
					after.push_back(barrier);
				}
				offsets.push_back(offset);
				offset += AlignUp(item.bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			}
			if (!before.empty()) list_->ResourceBarrier((UINT)before.size(), before.data());
			for (size_t i = at; i < end; ++i) {
				CopyItem& item = items[i];
				ID3D12Resource* resource = (ID3D12Resource*)item.object->real;
				const u64 dstOffset = offsets[i - at];
				if (item.object->resource->IsBuffer()) {
					list_->CopyBufferRegion(readback, dstOffset, resource, 0, item.bytes);
					continue;
				}
				D3D12_TEXTURE_COPY_LOCATION dst = {};
				dst.pResource = readback;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				dst.PlacedFootprint = item.footprint;
				dst.PlacedFootprint.Offset = dstOffset;
				D3D12_TEXTURE_COPY_LOCATION src = {};
				src.pResource = resource;
				src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				src.SubresourceIndex = item.subresource;
				list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
			if (!after.empty()) list_->ResourceBarrier((UINT)after.size(), after.data());
			Execute();
			u8* mapped = nullptr;
			const D3D12_RANGE read = {0, (SIZE_T)used};
			if (SUCCEEDED(readback->Map(0, &read, (void**)&mapped))) {
				for (size_t i = at; i < end; ++i) {
					CopyItem& item = items[i];
					InitialContents contents = {};
					contents.id = item.object->id;
					contents.subresource = item.subresource;
					contents.footprint = item.footprint.Footprint;
					contents.data = mapped + offsets[i - at];
					contents.size = item.bytes;
					FileRecord(kInitialContents, contents);
				}
				const D3D12_RANGE none = {0, 0};
				readback->Unmap(0, &none);
			}
			readback->Release();
			at = end;
		}
	}

	// --- During the frame ---------------------------------------------------------------------------------------------

	void FrameCapture::DiffUploads() {
		Tracker& tracker = device_.Objects();
		for (Object* object : tracker.Live()) {
			if (!object->alive || !object->resource || !object->resource->CpuWritable()) continue;
			Shadow* shadow = nullptr;
			for (Shadow& known : shadows_)
				if (known.id == object->id) shadow = &known;
			ID3D12Resource* resource = (ID3D12Resource*)object->real;
			const size_t size = (size_t)object->resource->desc.Width;
			void* mapped = nullptr;
			const D3D12_RANGE read = {0, size};
			if (FAILED(resource->Map(0, &read, &mapped))) continue;
			const u8* now = (const u8*)mapped;
			if (!shadow) {
				// Made during the frame: all of it is new.
				shadows_.push_back({object->id, resource, std::vector<u8>(now, now + size)});
				MapWrite write = {object->id, 0, shadows_.back().bytes.data(), size};
				Record(kMapWrite, write);
			}
			else {
				size_t at = 0;
				while (at < size) {
					const size_t block = std::min(kDiffBlock, size - at);
					if (memcmp(now + at, shadow->bytes.data() + at, block) == 0) {
						at += block;
						continue;
					}
					size_t end = at + block;
					while (end < size) {
						const size_t next = std::min(kDiffBlock, size - end);
						if (memcmp(now + end, shadow->bytes.data() + end, next) == 0) break;
						end += next;
					}
					memcpy(shadow->bytes.data() + at, now + at, end - at);
					MapWrite write = {object->id, at, shadow->bytes.data() + at, end - at};
					Record(kMapWrite, write);
					at = end;
				}
			}
			const D3D12_RANGE none = {0, 0};
			resource->Unmap(0, &none);
		}
	}

	void FrameCapture::BeforeExecute(WrappedQueue* queue, const std::vector<WrappedList*>& lists) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		DiffUploads();
		std::vector<ListContents> contents(lists.size());
		for (size_t i = 0; i < lists.size(); ++i) {
			if (!lists[i]) {
				Warn("a command list made outside the wrapped device was submitted; its commands are missing");
				continue;
			}
			ListRecording& recording = lists[i]->Recording();
			contents[i] = {lists[i]->Id(), recording.commands.Data(), recording.commands.Size(), recording.count};
			events_ += recording.count;
			for (const std::string& method : recording.unsupported) Warn(method + " is not captured yet; replay skips it");
			if (recording.bundles) Warn("bundles are not captured yet; replay skips ExecuteBundle");
		}
		Submit submit = {queue->Id(), (u32)contents.size(), contents.data()};
		Record(kSubmit, submit);
	}

	void FrameCapture::QueueFence(u32 recordType, WrappedQueue* queue, ID3D12Fence* fence, u64 value) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		fb::d3d12::QueueFence op = {queue->Id(), device_.Objects().Id(fence), value};
		Record(recordType, op);
		events_ += 1;
	}

	void FrameCapture::QueueMarker(WrappedQueue* queue, MarkerOp op, UINT metadata, const void* data, UINT size) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		fb::d3d12::QueueMarker marker = {queue->Id(), op, metadata, data, size};
		Record(kQueueMarker, marker);
		events_ += 1;
	}

	void FrameCapture::Created(Object* object) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_ || !object->recordType || object->record.empty()) return;
		RecordHeader header = {object->recordType, 0, object->record.size()};
		stream_.Put(header);
		stream_.Bytes(object->record.data(), object->record.size());
	}

	void FrameCapture::DescriptorWritten(const DescriptorWrite& view) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		DescriptorWrite copy = view;
		Record(kDescriptor, copy);
	}

	void FrameCapture::DescriptorsCopied(const std::vector<DescriptorCopy>& runs) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		if (!active_) return;
		for (DescriptorCopy run : runs) Record(kCopyDescriptors, run);
	}

	// --- The frame's end ----------------------------------------------------------------------------------------------

	void FrameCapture::Thumbnail(WrappedSwapChain* swapChain, CaptureSummary& summary) {
		u64 bufferId = 0;
		ID3D12Resource* buffer = swapChain ? swapChain->CurrentBuffer(bufferId) : nullptr;
		if (!buffer) return;
		Object* object = device_.Objects().FindId(bufferId);
		const D3D12_RESOURCE_DESC desc = buffer->GetDesc();
		summary.width = (u32)desc.Width;
		summary.height = desc.Height;
		const FormatInfo* info = DxgiFormat(DxgiDefaultView(desc.Format));
		if (!info || IsBlock(info->codec)) {
			buffer->Release();
			return;
		}
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT rows = 0;
		UINT64 rowBytes = 0, total = 0;
		device_.Real()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
		D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_READBACK};
		D3D12_RESOURCE_DESC bufferDesc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
		ID3D12Resource* readback = nullptr;
		if (FAILED(device_.Real()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&readback))) {
			buffer->Release();
			return;
		}
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PRESENT;
		if (object && object->resource && !object->resource->states.empty()) state = object->resource->states[0];
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = buffer;
		barrier.Transition.Subresource = 0;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) list_->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = readback;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dst.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = buffer;
		src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) list_->ResourceBarrier(1, &barrier);
		Execute();
		u8* mapped = nullptr;
		const D3D12_RANGE read = {0, (SIZE_T)total};
		if (SUCCEEDED(readback->Map(0, &read, (void**)&mapped))) {
			const u32 width = (u32)desc.Width, height = desc.Height;
			const u32 step = std::max<u32>(1, (width + 479) / 480);
			const u32 outWidth = width / step, outHeight = height / step;
			std::vector<float> row((size_t)width * 4);
			std::vector<u8> pixels((size_t)outWidth * outHeight * 4);
			const bool linear = info->component == Component::Float || info->component == Component::Srgb;
			for (u32 y = 0; y < outHeight; ++y) {
				DecodeRows(*info, mapped + footprint.Footprint.RowPitch * (size_t)(y * step), footprint.Footprint.RowPitch, width, 1, row.data());
				for (u32 x = 0; x < outWidth; ++x) {
					for (int c = 0; c < 4; ++c) {
						float v = row[(size_t)(x * step) * 4 + c];
						if (std::isnan(v)) v = 0;
						if (c < 3 && linear) v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
						if (c == 3) v = 1.0f;
						pixels[((size_t)y * outWidth + x) * 4 + c] = (u8)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
					}
				}
			}
			const D3D12_RANGE none = {0, 0};
			readback->Unmap(0, &none);
			summary.thumbnail = WritePng(base_ + L".png", outWidth, outHeight, pixels.data(), (size_t)outWidth * 4);
			ByteWriter thumbnail;
			thumbnail.Put(outWidth);
			thumbnail.Put(outHeight);
			thumbnail.Bytes(pixels.data(), pixels.size());
			file_.Record(kRecordThumbnail, thumbnail);
		}
		readback->Release();
		buffer->Release();
	}

	std::string FrameCapture::MetaJson(const CaptureSummary& summary) {
		JsonWriter json;
		json.BeginObject();
		json.String("program", ExecutableName());
		json.String("programPath", Narrow(ExecutablePath()));
		json.Uint("pid", GetCurrentProcessId());
		json.String("api", "D3D12");
		json.Uint("frame", summary.frame);
		json.String("time", TimeStamp());
		json.String("gpu", summary.gpu);
		json.String("driver", summary.driver);
		json.Uint("width", summary.width);
		json.Uint("height", summary.height);
		json.Uint("events", summary.events);
		json.String("comment", Session::Get().Comment());
		json.BeginArray("warnings");
		for (const std::string& warning : summary.warnings) json.String(warning);
		json.EndArray();
		json.BeginObject("annotations");
		for (const auto& annotation : Session::Get().Annotations()) json.String(annotation.first.c_str(), annotation.second);
		json.EndObject();
		json.EndObject();
		return json.Take();
	}

	void FrameCapture::End(WrappedSwapChain* swapChain, UINT syncInterval, UINT flags) {
		u64 bufferId = 0;
		if (swapChain) {
			ID3D12Resource* buffer = swapChain->CurrentBuffer(bufferId);
			if (buffer) buffer->Release();
		}
		fb::d3d12::Present present = {swapChain ? swapChain->Id() : 0, bufferId, syncInterval, flags};
		Record(kPresent, present);
		events_ += 1;
		active_ = false;
		Session::Get().SetCapturing(false);
		WaitIdle();
		CaptureSummary summary;
		summary.api = "D3D12";
		summary.frame = frame_;
		summary.events = events_;
		DeviceInfo info = {};
		AdapterInfo(device_.Real(), info);
		summary.gpu = info.adapter;
		summary.driver = info.driver;
		Thumbnail(swapChain, summary);
		summary.warnings = warnings_;
		file_.Append(stream_.Data(), stream_.Size());
		file_.Record(kRecordFrameEnd, nullptr, 0);
		for (const auto& annotation : Session::Get().Annotations()) {
			ByteWriter payload;
			std::string key = annotation.first, value = annotation.second;
			Ser s(payload, MapForRecords());
			s.Text("key", key);
			s.Text("value", value);
			file_.Record(kRecordAnnotation, payload);
		}
		for (const std::string& warning : warnings_) file_.Record(kRecordWarning, warning.data(), warning.size());
		const std::string meta = MetaJson(summary);
		file_.Record(kRecordMeta, meta.data(), meta.size());
		const u64 bytes = file_.Written();
		std::string error;
		if (!file_.Close(error)) {
			Session::Get().Failed(error);
			return;
		}
		stream_.Clear();
		shadows_.clear();
		Session::Get().Written(base_, bytes, summary);
	}
}
