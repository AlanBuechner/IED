#include "device.h"

#include <windows.h>
#include <algorithm>

namespace fb::replay::d3d12
{
	static D3D12_RESOURCE_DESC Desc0(const D3D12_RESOURCE_DESC1& d) {
		return {d.Dimension, d.Alignment, d.Width, d.Height, d.DepthOrArraySize, d.MipLevels, d.Format, d.SampleDesc, d.Layout, d.Flags};
	}

	static bool CpuWritable(const D3D12_HEAP_PROPERTIES& heap, const D3D12_RESOURCE_DESC1& desc) {
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return false;
		if (heap.Type == D3D12_HEAP_TYPE_UPLOAD || heap.Type == D3D12_HEAP_TYPE_GPU_UPLOAD) return true;
		return heap.Type == D3D12_HEAP_TYPE_CUSTOM && (heap.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE || heap.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
	}

	ReplayDevice::~ReplayDevice() {
		if (device_) {
			if (utilityQueue_) WaitQueue(utilityQueue_);
			for (auto& queue : queues_) WaitQueue(queue.second.queue);
		}
		for (auto& set : lists_) {
			if (set.second.list) set.second.list->Release();
			if (set.second.allocator) set.second.allocator->Release();
		}
		if (utility_.list) utility_.list->Release();
		if (utility_.allocator) utility_.allocator->Release();
		if (pristine_) pristine_->Release();
		for (auto& object : objects_)
			if (object.second) object.second->Release();
		if (utilityQueue_) utilityQueue_->Release();
		if (fence_) fence_->Release();
		if (event_) CloseHandle((HANDLE)event_);
		if (infoQueue_) infoQueue_->Release();
		if (device2_) device2_->Release();
		if (device_) device_->Release();
		if (adapter_) adapter_->Release();
	}

	// --- Device and objects ----------------------------------------------------------------------------------------------

	void ReplayDevice::PickAdapter(const JsonValue& options) {
		IDXGIFactory6* factory = nullptr;
		if (FAILED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory6), (void**)&factory))) Fail("DXGI is unavailable");
		const bool warp = options["warp"].Bool(false);
		IDXGIAdapter1* chosen = nullptr;
		IDXGIAdapter1* sameModel = nullptr;
		for (UINT i = 0;; ++i) {
			IDXGIAdapter1* adapter = nullptr;
			if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
			const u64 luid = ((u64)(u32)desc.AdapterLuid.HighPart << 32) | desc.AdapterLuid.LowPart;
			if (warp ? software : (!software && luid == model_.device.luid)) {
				chosen = adapter;
				break;
			}
			if (!warp && !software && !sameModel && desc.VendorId == model_.device.vendorId && desc.DeviceId == model_.device.deviceId) {
				sameModel = adapter;
				continue;
			}
			adapter->Release();
		}
		if (!chosen) chosen = sameModel, sameModel = nullptr;
		if (sameModel) sameModel->Release();
		if (!chosen && SUCCEEDED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&chosen))) {
			warnings.push_back("the capture's GPU is not in this machine; replaying on another one");
		}
		factory->Release();
		if (!chosen) Fail("no GPU to replay on");
		adapter_ = chosen;
		DXGI_ADAPTER_DESC1 desc;
		adapter_->GetDesc1(&desc);
		adapterName = Narrow(desc.Description);
		LARGE_INTEGER umd;
		if (SUCCEEDED(adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
			const u64 v = (u64)umd.QuadPart;
			driver = Format("%u.%u.%u.%u", (u32)(v >> 48), (u32)((v >> 32) & 0xFFFF), (u32)((v >> 16) & 0xFFFF), (u32)(v & 0xFFFF));
		}
	}

	void ReplayDevice::Create(const JsonValue& options) {
		PickAdapter(options);
		if (options["debugLayer"].Bool(false)) {
			ID3D12Debug* debug = nullptr;
			if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&debug))) {
				debug->EnableDebugLayer();
				debug->Release();
			}
			else warnings.push_back("the D3D12 debug layer is not installed (Windows optional feature Graphics Tools)");
		}
		const HRESULT hr = D3D12CreateDevice(adapter_, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device_);
		if (FAILED(hr)) Fail("D3D12CreateDevice failed on %s (0x%08lx)", adapterName.c_str(), (unsigned long)hr);
		device_->QueryInterface(__uuidof(ID3D12Device2), (void**)&device2_);
		if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&infoQueue_))) infoQueue_->SetMessageCountLimit(4096);
		device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence_);
		event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		D3D12_COMMAND_QUEUE_DESC desc = {D3D12_COMMAND_LIST_TYPE_DIRECT};
		if (FAILED(device_->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), (void**)&utilityQueue_))) Fail("cannot make a command queue");
		device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&utility_.allocator);
		device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, utility_.allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&utility_.list);
		utility_.list->Close();

		for (const auto& entry : model_.objects) CreateObject(entry.second);
		for (const FrameOp& op : model_.frame)
			if (op.kind == OpKind::Descriptor || op.kind == OpKind::CopyDescriptors) frameWritesDescriptors_ = true;
		PrepareInitialContents();
	}

	void ReplayDevice::CreateObject(const ObjectEntry& entry) {
		switch (entry.kind) {
			case ObjectKind::Heap: {
				HeapCreate heap = {};
				model_.Decode(entry.record, entry.recordSize, heap, this);
				ID3D12Heap* made = nullptr;
				if (FAILED(device_->CreateHeap(&heap.desc, __uuidof(ID3D12Heap), (void**)&made))) {
					warnings.push_back(Format("heap %s could not be made", model_.NameOf(entry.id).c_str()));
					return;
				}
				objects_[entry.id] = made;
				return;
			}
			case ObjectKind::Resource: CreateResource(entry); return;
			case ObjectKind::DescriptorHeap: {
				DescriptorHeapCreate heap = {};
				model_.Decode(entry.record, entry.recordSize, heap, this);
				ID3D12DescriptorHeap* made = nullptr;
				if (FAILED(device_->CreateDescriptorHeap(&heap.desc, __uuidof(ID3D12DescriptorHeap), (void**)&made))) {
					warnings.push_back(Format("descriptor heap %s could not be made", model_.NameOf(entry.id).c_str()));
					return;
				}
				objects_[entry.id] = made;
				HeapInfo info;
				info.heap = made;
				info.cpu = made->GetCPUDescriptorHandleForHeapStart().ptr;
				info.gpu = (heap.desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ? made->GetGPUDescriptorHandleForHeapStart().ptr : 0;
				info.increment = device_->GetDescriptorHandleIncrementSize(heap.desc.Type);
				heaps_[entry.id] = info;
				return;
			}
			case ObjectKind::RootSignature: {
				RootSignatureCreate signature = {};
				model_.Decode(entry.record, entry.recordSize, signature, this);
				ID3D12RootSignature* made = nullptr;
				if (FAILED(device_->CreateRootSignature(signature.nodeMask, signature.blob, (SIZE_T)signature.blobSize, __uuidof(ID3D12RootSignature), (void**)&made))) {
					warnings.push_back(Format("root signature %s could not be made", model_.NameOf(entry.id).c_str()));
					return;
				}
				objects_[entry.id] = made;
				return;
			}
			case ObjectKind::PipelineState: CreatePipeline(entry); return;
			case ObjectKind::CommandSignature: {
				CommandSignatureCreate signature = {};
				model_.Decode(entry.record, entry.recordSize, signature, this);
				ID3D12CommandSignature* made = nullptr;
				if (FAILED(device_->CreateCommandSignature(&signature.desc, signature.rootSignature, __uuidof(ID3D12CommandSignature), (void**)&made))) {
					warnings.push_back(Format("command signature %s could not be made", model_.NameOf(entry.id).c_str()));
					return;
				}
				objects_[entry.id] = made;
				return;
			}
			case ObjectKind::QueryHeap: {
				QueryHeapCreate heap = {};
				model_.Decode(entry.record, entry.recordSize, heap, this);
				ID3D12QueryHeap* made = nullptr;
				if (SUCCEEDED(device_->CreateQueryHeap(&heap.desc, __uuidof(ID3D12QueryHeap), (void**)&made))) objects_[entry.id] = made;
				return;
			}
			case ObjectKind::Fence: {
				FenceCreate fence = {};
				model_.Decode(entry.record, entry.recordSize, fence, this);
				ID3D12Fence* made = nullptr;
				if (SUCCEEDED(device_->CreateFence(fence.value, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&made))) objects_[entry.id] = made;
				return;
			}
			case ObjectKind::CommandQueue: {
				QueueCreate queue = {};
				model_.Decode(entry.record, entry.recordSize, queue, this);
				queue.desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queue.desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				ID3D12CommandQueue* made = nullptr;
				if (FAILED(device_->CreateCommandQueue(&queue.desc, __uuidof(ID3D12CommandQueue), (void**)&made))) {
					warnings.push_back(Format("queue %s could not be made", model_.NameOf(entry.id).c_str()));
					return;
				}
				objects_[entry.id] = made;
				queues_[entry.id] = {made, queue.desc.Type};
				return;
			}
			default: return;
		}
	}

	void ReplayDevice::CreateResource(const ObjectEntry& entry) {
		ResourceCreate record = {};
		if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
		D3D12_HEAP_PROPERTIES heap = record.heapProperties;
		D3D12_HEAP_FLAGS heapFlags = record.heapFlags;
		if (record.kind == ResourceKind::SwapChainBuffer) {
			heap = {D3D12_HEAP_TYPE_DEFAULT};
			heapFlags = D3D12_HEAP_FLAG_NONE;
		}
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		if (heap.Type == D3D12_HEAP_TYPE_UPLOAD) state = D3D12_RESOURCE_STATE_GENERIC_READ;
		else if (heap.Type == D3D12_HEAP_TYPE_READBACK) state = D3D12_RESOURCE_STATE_COPY_DEST;
		const D3D12_RESOURCE_DESC desc = Desc0(record.desc);
		const D3D12_CLEAR_VALUE* clear = record.hasClearValue ? &record.clearValue : nullptr;
		ID3D12Resource* made = nullptr;
		HRESULT hr = E_FAIL;
		switch (record.kind) {
			case ResourceKind::Placed: {
				ID3D12Heap* placedIn = (ID3D12Heap*)Object(record.heap);
				if (placedIn) hr = device_->CreatePlacedResource(placedIn, record.heapOffset, &desc, state, clear, __uuidof(ID3D12Resource), (void**)&made);
				break;
			}
			case ResourceKind::Reserved: hr = device_->CreateReservedResource(&desc, state, clear, __uuidof(ID3D12Resource), (void**)&made); break;
			default: hr = device_->CreateCommittedResource(&heap, heapFlags, &desc, state, clear, __uuidof(ID3D12Resource), (void**)&made); break;
		}
		if (FAILED(hr)) {
			warnings.push_back(Format("resource %s could not be made (0x%08lx)", model_.NameOf(entry.id).c_str(), (unsigned long)hr));
			return;
		}
		const std::string name = model_.NameOf(entry.id);
		made->SetName(Widen(name).c_str());
		objects_[entry.id] = made;
		ids_[made] = entry.id;
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) addresses_[entry.id] = made->GetGPUVirtualAddress();
		if (CpuWritable(heap, record.desc)) {
			u8* mapped = nullptr;
			const D3D12_RANGE none = {0, 0};
			if (SUCCEEDED(made->Map(0, &none, (void**)&mapped))) mapped_[entry.id] = mapped;
		}
		u32 subresources = 1;
		if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) {
			D3D12_FEATURE_DATA_FORMAT_INFO info = {desc.Format, 1};
			if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &info, sizeof(info))) || !info.PlaneCount) info.PlaneCount = 1;
			subresources = made->GetDesc().MipLevels * (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : desc.DepthOrArraySize) * info.PlaneCount;
		}
		subresources_[entry.id] = subresources;
		states_[entry.id].assign(1, state);
	}

	template <class T> static void AddSubobject(std::vector<u8>& stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const T& value) {
		const size_t at = (size_t)AlignUp(stream.size(), sizeof(void*));
		const size_t inner = (size_t)AlignUp(at + sizeof(type), alignof(T) > 4 ? alignof(T) : 4);
		stream.resize((size_t)AlignUp(inner + sizeof(T), sizeof(void*)));
		memcpy(stream.data() + at, &type, sizeof(type));
		memcpy(stream.data() + inner, &value, sizeof(T));
	}

	void ReplayDevice::CreatePipeline(const ObjectEntry& entry) {
		PipelineCreate p = {};
		if (!model_.Decode(entry.record, entry.recordSize, p, this)) return;
		HRESULT hr = E_FAIL;
		ID3D12PipelineState* made = MakePipeline(p, hr);
		if (!made) {
			warnings.push_back(Format("pipeline state %s could not be made (0x%08lx)", model_.NameOf(entry.id).c_str(), (unsigned long)hr));
			return;
		}
		made->SetName(Widen(model_.NameOf(entry.id)).c_str());
		objects_[entry.id] = made;
	}

	ID3D12PipelineState* ReplayDevice::Variant(u64 pipeline, const std::function<void(PipelineCreate&)>& change) {
		const ObjectEntry* entry = model_.Find(pipeline);
		PipelineCreate p = {};
		if (!entry || !model_.Decode(entry->record, entry->recordSize, p, this)) return nullptr;
		change(p);
		HRESULT hr = E_FAIL;
		return MakePipeline(p, hr);
	}

	void ReplayDevice::Repeat(ID3D12GraphicsCommandList* list, const CommandRef& command) {
		std::vector<u8> args;
		RunCommand(list, command, args);
	}

	ID3D12PipelineState* ReplayDevice::MakePipeline(const PipelineCreate& p, HRESULT& hr) {
		ID3D12PipelineState* made = nullptr;
		if (device2_) {
			std::vector<u8> stream;
			AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, p.rootSignature);
			const std::pair<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE, const D3D12_SHADER_BYTECODE*> shaders[] = {
				{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, &p.vs}, {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, &p.ps}, {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, &p.ds},
				{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, &p.hs}, {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, &p.gs}, {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, &p.cs},
				{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, &p.as}, {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, &p.ms}};
			for (const auto& shader : shaders)
				if (shader.second->pShaderBytecode && shader.second->BytecodeLength) AddSubobject(stream, shader.first, *shader.second);
			AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, p.nodeMask);
			AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, p.flags);
			if (p.kind != PipelineKind::Compute) {
				if (p.streamOutput.NumEntries) AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, p.streamOutput);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, p.blend);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, p.sampleMask);
				const D3D12_RASTERIZER_DESC2& r = p.rasterizer;
				const BOOL multisample = r.LineRasterizationMode == D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE || r.LineRasterizationMode == D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW;
				const BOOL antialiased = r.LineRasterizationMode == D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
				if (p.rasterizerVersion == 2) AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2, r);
				else if (p.rasterizerVersion == 1) {
					const D3D12_RASTERIZER_DESC1 r1 = {r.FillMode, r.CullMode, r.FrontCounterClockwise, r.DepthBias, r.DepthBiasClamp, r.SlopeScaledDepthBias, r.DepthClipEnable, multisample, antialiased, r.ForcedSampleCount, r.ConservativeRaster};
					AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, r1);
				}
				else {
					const D3D12_RASTERIZER_DESC r0 = {r.FillMode, r.CullMode, r.FrontCounterClockwise, (INT)r.DepthBias, r.DepthBiasClamp, r.SlopeScaledDepthBias, r.DepthClipEnable, multisample, antialiased, r.ForcedSampleCount, r.ConservativeRaster};
					AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, r0);
				}
				const D3D12_DEPTH_STENCIL_DESC2& d = p.depthStencil;
				auto op0 = [](const D3D12_DEPTH_STENCILOP_DESC1& o) { return D3D12_DEPTH_STENCILOP_DESC{o.StencilFailOp, o.StencilDepthFailOp, o.StencilPassOp, o.StencilFunc}; };
				if (p.depthStencilVersion == 2) AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2, d);
				else if (p.depthStencilVersion == 1) {
					const D3D12_DEPTH_STENCIL_DESC1 d1 = {d.DepthEnable, d.DepthWriteMask, d.DepthFunc, d.StencilEnable, d.FrontFace.StencilReadMask, d.FrontFace.StencilWriteMask, op0(d.FrontFace), op0(d.BackFace), d.DepthBoundsTestEnable};
					AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, d1);
				}
				else {
					const D3D12_DEPTH_STENCIL_DESC d0 = {d.DepthEnable, d.DepthWriteMask, d.DepthFunc, d.StencilEnable, d.FrontFace.StencilReadMask, d.FrontFace.StencilWriteMask, op0(d.FrontFace), op0(d.BackFace)};
					AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, d0);
				}
				if (p.inputLayout.NumElements) AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, p.inputLayout);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, p.stripCut);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, p.topologyType);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, p.renderTargets);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, p.depthStencilFormat);
				AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, p.sampleDesc);
				if (p.viewInstancing.ViewInstanceCount) AddSubobject(stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, p.viewInstancing);
			}
			const D3D12_PIPELINE_STATE_STREAM_DESC desc = {stream.size(), stream.data()};
			hr = device2_->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), (void**)&made);
		}
		else if (p.kind == PipelineKind::Compute) {
			D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {p.rootSignature, p.cs, p.nodeMask, {}, p.flags};
			hr = device_->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), (void**)&made);
		}
		return SUCCEEDED(hr) ? made : nullptr;
	}

	// --- Descriptors ---------------------------------------------------------------------------------------------------

	void ReplayDevice::WriteDescriptor(const u8* data, u64 size) {
		DescriptorWrite write = {};
		if (!model_.Decode(data, size, write, this)) return;
		auto heap = heaps_.find(write.heap);
		if (heap == heaps_.end()) return;
		const D3D12_CPU_DESCRIPTOR_HANDLE handle = {(SIZE_T)(heap->second.cpu + (u64)write.index * heap->second.increment)};
		switch (write.kind) {
			case ViewKind::Cbv: device_->CreateConstantBufferView(write.hasDesc ? &write.cbv : nullptr, handle); break;
			case ViewKind::Srv:
				if (write.resource || write.hasDesc) device_->CreateShaderResourceView(write.resource, write.hasDesc ? &write.srv : nullptr, handle);
				break;
			case ViewKind::Uav:
				if (write.resource || write.hasDesc) device_->CreateUnorderedAccessView(write.resource, write.counter, write.hasDesc ? &write.uav : nullptr, handle);
				break;
			case ViewKind::Rtv:
				if (write.resource || write.hasDesc) device_->CreateRenderTargetView(write.resource, write.hasDesc ? &write.rtv : nullptr, handle);
				break;
			case ViewKind::Dsv:
				if (write.resource || write.hasDesc) device_->CreateDepthStencilView(write.resource, write.hasDesc ? &write.dsv : nullptr, handle);
				break;
			case ViewKind::Sampler: {
				const D3D12_SAMPLER_DESC2& s = write.sampler;
				D3D12_SAMPLER_DESC desc = {s.Filter, s.AddressU, s.AddressV, s.AddressW, s.MipLODBias, s.MaxAnisotropy, s.ComparisonFunc,
					{s.FloatBorderColor[0], s.FloatBorderColor[1], s.FloatBorderColor[2], s.FloatBorderColor[3]}, s.MinLOD, s.MaxLOD};
				device_->CreateSampler(&desc, handle);
				break;
			}
			case ViewKind::Sampler2: {
				ID3D12Device11* device11 = nullptr;
				if (SUCCEEDED(device_->QueryInterface(__uuidof(ID3D12Device11), (void**)&device11))) {
					device11->CreateSampler2(&write.sampler, handle);
					device11->Release();
				}
				break;
			}
			default: break;
		}
	}

	void ReplayDevice::CopyDescriptors(const u8* data, u64 size) {
		DescriptorCopy copy = {};
		if (!model_.Decode(data, size, copy, this) || !copy.srcHeap) return;
		auto dst = heaps_.find(copy.dstHeap);
		auto src = heaps_.find(copy.srcHeap);
		if (dst == heaps_.end() || src == heaps_.end()) return;
		const D3D12_DESCRIPTOR_HEAP_TYPE type = dst->second.heap->GetDesc().Type;
		const D3D12_CPU_DESCRIPTOR_HANDLE to = {(SIZE_T)(dst->second.cpu + (u64)copy.dstIndex * dst->second.increment)};
		const D3D12_CPU_DESCRIPTOR_HANDLE from = {(SIZE_T)(src->second.cpu + (u64)copy.srcIndex * src->second.increment)};
		device_->CopyDescriptorsSimple(copy.count, to, from, type);
	}

	// --- Initial contents ---------------------------------------------------------------------------------------------

	void ReplayDevice::PrepareInitialContents() {
		u64 total = 0;
		for (const auto& entry : model_.initial) {
			if (mapped_.count(entry.first) || !objects_.count(entry.first)) continue;
			for (const InitialData& data : entry.second) {
				pristineItems_.push_back({entry.first, data.subresource, total, data.footprint});
				total += AlignUp(data.size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			}
		}
		if (!total) return;
		D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_UPLOAD};
		D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
		if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&pristine_))) {
			warnings.push_back(Format("not enough memory for %llu MB of initial contents; resources start empty", (unsigned long long)(total >> 20)));
			pristineItems_.clear();
			return;
		}
		const D3D12_RANGE none = {0, 0};
		pristine_->Map(0, &none, (void**)&pristineMapped_);
		size_t item = 0;
		for (const auto& entry : model_.initial) {
			if (mapped_.count(entry.first) || !objects_.count(entry.first)) continue;
			for (const InitialData& data : entry.second) memcpy(pristineMapped_ + pristineItems_[item++].offset, data.data, (size_t)data.size);
		}
	}

	static D3D12_RESOURCE_STATES StartStateOf(const Model& model, u64 id, u32 subresource, D3D12_RESOURCE_STATES fallback) {
		auto found = model.states.find(id);
		if (found == model.states.end() || found->second.empty()) return fallback;
		return subresource < found->second.size() ? found->second[subresource] : found->second[0];
	}

	void ReplayDevice::Reset() {
		for (const auto& entry : model_.initial) {
			auto mapped = mapped_.find(entry.first);
			if (mapped == mapped_.end()) continue;
			for (const InitialData& data : entry.second) memcpy(mapped->second, data.data, (size_t)data.size);
		}
		ID3D12GraphicsCommandList* list = BeginUtility();
		std::vector<D3D12_RESOURCE_BARRIER> before, after;
		auto transition = [](ID3D12Resource* resource, u32 sub, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource;
			barrier.Transition.Subresource = sub;
			barrier.Transition.StateBefore = from;
			barrier.Transition.StateAfter = to;
			return barrier;
		};
		std::unordered_map<u64, bool> copied;
		for (const Pristine& item : pristineItems_) copied[item.id] = true;
		// Every GPU resource back to its state at the frame's start; those with initial contents through COPY_DEST.
		for (auto& entry : states_) {
			const u64 id = entry.first;
			ID3D12Resource* resource = Resource(id);
			if (!resource || mapped_.count(id)) continue;
			const u32 subresources = subresources_[id];
			std::vector<D3D12_RESOURCE_STATES>& now = entry.second;
			const bool viaCopy = copied.count(id) != 0;
			for (u32 sub = 0; sub < (now.size() == 1 ? 1u : subresources); ++sub) {
				const u32 barrierSub = now.size() == 1 ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : sub;
				const D3D12_RESOURCE_STATES current = now.size() == 1 ? now[0] : now[sub];
				const D3D12_RESOURCE_STATES start = StartStateOf(model_, id, barrierSub == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ? 0 : sub, current);
				if (viaCopy) {
					if (current != D3D12_RESOURCE_STATE_COPY_DEST) before.push_back(transition(resource, barrierSub, current, D3D12_RESOURCE_STATE_COPY_DEST));
				}
				else if (current != start) before.push_back(transition(resource, barrierSub, current, start));
			}
			auto starts = model_.states.find(id);
			if (viaCopy) {
				if (starts == model_.states.end() || starts->second.size() <= 1) {
					const D3D12_RESOURCE_STATES start = starts == model_.states.end() || starts->second.empty() ? D3D12_RESOURCE_STATE_COMMON : starts->second[0];
					if (start != D3D12_RESOURCE_STATE_COPY_DEST) after.push_back(transition(resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, start));
				}
				else {
					for (u32 sub = 0; sub < starts->second.size(); ++sub)
						if (starts->second[sub] != D3D12_RESOURCE_STATE_COPY_DEST) after.push_back(transition(resource, sub, D3D12_RESOURCE_STATE_COPY_DEST, starts->second[sub]));
				}
			}
			if (starts != model_.states.end() && !starts->second.empty()) now = starts->second;
			else if (viaCopy) now.assign(1, D3D12_RESOURCE_STATE_COMMON);
		}
		if (!before.empty()) list->ResourceBarrier((UINT)before.size(), before.data());
		for (const Pristine& item : pristineItems_) {
			ID3D12Resource* resource = Resource(item.id);
			if (!resource) continue;
			if (resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
				list->CopyBufferRegion(resource, 0, pristine_, item.offset, item.footprint.Width);
				continue;
			}
			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = resource;
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = item.subresource;
			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = pristine_;
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint.Offset = item.offset;
			src.PlacedFootprint.Footprint = item.footprint;
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
		if (!after.empty()) list->ResourceBarrier((UINT)after.size(), after.data());
		RunUtility();
		if (descriptorsDirty_) {
			for (const auto& heap : model_.descriptors)
				for (const auto& slot : heap.second) WriteDescriptor(slot.second.first, slot.second.second);
			descriptorsDirty_ = frameWritesDescriptors_;
		}
	}

	// --- Replaying -------------------------------------------------------------------------------------------------------

	ReplayDevice::ListSet& ReplayDevice::ListsFor(D3D12_COMMAND_LIST_TYPE type) {
		ListSet& set = lists_[(int)type];
		if (!set.list) {
			if (FAILED(device_->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator), (void**)&set.allocator)) ||
				FAILED(device_->CreateCommandList(0, type, set.allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&set.list)))
				Fail("cannot make a command list of type %d", (int)type);
			set.list->Close();
		}
		return set;
	}

	const ReplayDevice::QueueInfo* ReplayDevice::QueueOf(u64 id) const {
		auto found = queues_.find(id);
		return found == queues_.end() ? nullptr : &found->second;
	}

	D3D12_COMMAND_LIST_TYPE ReplayDevice::QueueType(u64 queue) const {
		const QueueInfo* info = QueueOf(queue);
		return info ? info->type : D3D12_COMMAND_LIST_TYPE_DIRECT;
	}

	u64 ReplayDevice::TimestampFrequency(u64 queue) const {
		const QueueInfo* info = QueueOf(queue);
		UINT64 frequency = 0;
		(info ? info->queue : utilityQueue_)->GetTimestampFrequency(&frequency);
		return frequency;
	}

	bool ReplayDevice::QueueSupportsTimestamps(u64 queue) const {
		if (QueueType(queue) != D3D12_COMMAND_LIST_TYPE_COPY) return true;
		D3D12_FEATURE_DATA_D3D12_OPTIONS3 options = {};
		return SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options))) && options.CopyQueueTimestampQueriesSupported;
	}

	void ReplayDevice::WaitQueue(ID3D12CommandQueue* queue) {
		const u64 value = ++fenceValue_;
		if (FAILED(queue->Signal(fence_, value))) return;
		if (fence_->GetCompletedValue() < value) {
			fence_->SetEventOnCompletion(value, (HANDLE)event_);
			if (WaitForSingleObject((HANDLE)event_, 60000) != WAIT_OBJECT_0) Fail("the GPU did not finish the replay within a minute (device removed?)");
		}
		const HRESULT removed = device_->GetDeviceRemovedReason();
		if (FAILED(removed)) Fail("the replay device was removed (0x%08lx)", (unsigned long)removed);
	}

	void ReplayDevice::Execute(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
		const HRESULT closed = list->Close();
		if (FAILED(closed)) Fail("a replayed command list did not close (0x%08lx); see Messages", (unsigned long)closed);
		ID3D12CommandList* lists[] = {list};
		queue->ExecuteCommandLists(1, lists);
		WaitQueue(queue);
	}

	ID3D12GraphicsCommandList* ReplayDevice::BeginUtility() {
		utility_.allocator->Reset();
		utility_.list->Reset(utility_.allocator, nullptr);
		return utility_.list;
	}

	void ReplayDevice::RunUtility() { Execute(utilityQueue_, utility_.list); }

	ID3D12Resource* ReplayDevice::Resource(u64 id) const {
		auto found = objects_.find(id);
		if (found == objects_.end()) return nullptr;
		const ObjectEntry* entry = model_.Find(id);
		return entry && entry->kind == ObjectKind::Resource ? (ID3D12Resource*)found->second : nullptr;
	}

	D3D12_RESOURCE_STATES ReplayDevice::StateOf(u64 id, u32 subresource) const {
		auto found = states_.find(id);
		if (found == states_.end() || found->second.empty()) return D3D12_RESOURCE_STATE_COMMON;
		return subresource < found->second.size() ? found->second[subresource] : found->second[0];
	}

	void* ReplayDevice::Object(u64 id) {
		auto found = objects_.find(id);
		return found == objects_.end() ? nullptr : found->second;
	}

	u64 ReplayDevice::Address(u64 id, u64 offset) {
		auto found = addresses_.find(id);
		return found == addresses_.end() ? 0 : found->second + offset;
	}

	u64 ReplayDevice::CpuDescriptor(u64 heap, u32 index) {
		auto found = heaps_.find(heap);
		return found == heaps_.end() ? 0 : found->second.cpu + (u64)index * found->second.increment;
	}

	u64 ReplayDevice::GpuDescriptor(u64 heap, u32 index) {
		auto found = heaps_.find(heap);
		return found == heaps_.end() || !found->second.gpu ? 0 : found->second.gpu + (u64)index * found->second.increment;
	}

	void ReplayDevice::CollectMessages(u32 eid) {
		if (!infoQueue_) return;
		const UINT64 count = infoQueue_->GetNumStoredMessages();
		for (UINT64 i = 0; i < count; ++i) {
			SIZE_T length = 0;
			if (FAILED(infoQueue_->GetMessage(i, nullptr, &length)) || !length) continue;
			std::vector<u8> bytes(length);
			D3D12_MESSAGE* message = (D3D12_MESSAGE*)bytes.data();
			if (FAILED(infoQueue_->GetMessage(i, message, &length))) continue;
			static const char* severities[] = {"corruption", "error", "warning", "info", "message"};
			messages.push_back({eid, message->Severity <= D3D12_MESSAGE_SEVERITY_MESSAGE ? severities[message->Severity] : "message",
				std::string(message->pDescription, message->DescriptionByteLength ? message->DescriptionByteLength - 1 : 0)});
		}
		infoQueue_->ClearStoredMessages();
	}

	void ReplayDevice::ReplayTo(u32 eid, ReplayHooks* hooks) {
		messages.clear();
		if (infoQueue_) infoQueue_->ClearStoredMessages();
		Reset();
		for (const FrameOp& op : model_.frame) {
			switch (op.kind) {
				case OpKind::Descriptor: WriteDescriptor(op.data, op.size); break;
				case OpKind::CopyDescriptors: CopyDescriptors(op.data, op.size); break;
				case OpKind::MapWrite: {
					MapWrite write = {};
					if (!model_.DecodeModel(op.data, op.size, write)) break;
					auto mapped = mapped_.find(write.resource);
					if (mapped != mapped_.end()) memcpy(mapped->second + write.offset, write.data, (size_t)write.size);
					break;
				}
				case OpKind::Submit: {
					if (op.eid > eid) return;
					RunSubmit(op, eid, hooks);
					u32 commands = 0;
					for (const ListRef& list : op.lists) commands += (u32)list.commands.size();
					if (eid < op.eid + commands) return;
					break;
				}
				case OpKind::Signal:
				case OpKind::Wait:
				case OpKind::QueueMarker:
				case OpKind::Present:
					if (op.eid > eid) return;
					break;
				default: break;
			}
		}
	}

	void ReplayDevice::RunSubmit(const FrameOp& op, u32 stopEid, ReplayHooks* hooks) {
		for (const ListRef& list : op.lists) {
			if (list.commands.empty()) continue;
			if (list.commands.front().eid > stopEid) return;
			RunList(op.queue, list, stopEid, hooks);
		}
	}

	void ReplayDevice::RunList(u64 queue, const ListRef& listRef, u32 stopEid, ReplayHooks* hooks) {
		const QueueInfo* info = QueueOf(queue);
		if (!info) {
			warnings.push_back("a submission to an unknown queue was skipped");
			return;
		}
		ListSet& set = ListsFor(info->type);
		set.allocator->Reset();
		ID3D12PipelineState* initial = nullptr;
		size_t first = 0;
		if (listRef.commands.front().command == Cmd::Reset) {
			ResetArgs reset = {};
			scratch_.Reset();
			model_.DecodeCommandIn(scratch_, listRef.commands.front(), &reset, this);
			initial = reset.pipelineState;
			first = 1;
		}
		ID3D12GraphicsCommandList* list = set.list;
		list->Reset(set.allocator, initial);
		int openEvents = 0;
		bool openPass = false;
		std::vector<u8> args;
		std::vector<std::pair<ID3D12QueryHeap*, std::pair<D3D12_QUERY_TYPE, UINT>>> openQueries;
		for (size_t i = first; i < listRef.commands.size(); ++i) {
			const CommandRef& command = listRef.commands[i];
			if (command.eid > stopEid) break;
			if (hooks) hooks->Before(list, command);
			RunCommand(list, command, args);
			if (hooks) hooks->After(list, command);
			switch (command.command) {
				case Cmd::BeginEvent: ++openEvents; break;
				case Cmd::EndEvent: --openEvents; break;
				case Cmd::BeginRenderPass: openPass = true; break;
				case Cmd::EndRenderPass: openPass = false; break;
				case Cmd::BeginQuery: {
					QueryArgs* query = (QueryArgs*)args.data();
					openQueries.push_back({query->heap, {query->type, query->index}});
					break;
				}
				case Cmd::EndQuery: {
					QueryArgs* query = (QueryArgs*)args.data();
					for (size_t q = 0; q < openQueries.size(); ++q)
						if (openQueries[q].first == query->heap && openQueries[q].second.second == query->index) {
							openQueries.erase(openQueries.begin() + (ptrdiff_t)q);
							break;
						}
					break;
				}
				case Cmd::ResourceBarrier: {
					BarrierArgs* barriers = (BarrierArgs*)args.data();
					for (UINT b = 0; b < barriers->count; ++b) {
						const D3D12_RESOURCE_BARRIER& barrier = barriers->barriers[b];
						if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || (barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)) continue;
						auto id = ids_.find(barrier.Transition.pResource);
						if (id == ids_.end()) continue;
						std::vector<D3D12_RESOURCE_STATES>& states = states_[id->second];
						const u32 count = subresources_[id->second];
						if (barrier.Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) states.assign(1, barrier.Transition.StateAfter);
						else {
							if (states.size() == 1 && count > 1) states.assign(count, states[0]);
							if (barrier.Transition.Subresource < states.size()) states[barrier.Transition.Subresource] = barrier.Transition.StateAfter;
						}
					}
					break;
				}
				default: break;
			}
		}
		// A list stopped inside a render pass, a query or a marker is closed as the API requires.
		ID3D12GraphicsCommandList4* list4 = nullptr;
		if (openPass && SUCCEEDED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), (void**)&list4))) {
			list4->EndRenderPass();
			list4->Release();
		}
		for (auto& query : openQueries) list->EndQuery(query.first, query.second.first, query.second.second);
		for (; openEvents > 0; --openEvents) list->EndEvent();
		if (hooks) hooks->EndList(list, info->type);
		Execute(info->queue, list);
		CollectMessages(listRef.commands.empty() ? 0 : std::min(stopEid, listRef.commands.back().eid));
	}
}
