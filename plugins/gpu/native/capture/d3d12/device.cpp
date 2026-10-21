// The device proxy: objects made through it are recorded (docs/22 "Capture"), queues and command lists come back as
// proxies, and descriptor writes update the tracker's shadow of every heap.
#include "wrappers.h"

#include <unordered_map>

namespace fb::capture::d3d12
{
	// {9F3A1B52-6C7D-4E80-A1B2-C3D4E5F60718}
	const GUID kDeviceProxyIid = {0x9f3a1b52, 0x6c7d, 0x4e80, {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18}};

	namespace
	{
		std::mutex& RegistryLock() {
			static std::mutex lock;
			return lock;
		}

		std::unordered_map<ID3D12Device*, WrappedDevice*>& Registry() {
			static std::unordered_map<ID3D12Device*, WrappedDevice*> devices;
			return devices;
		}

		D3D12_RASTERIZER_DESC2 Rasterizer2(const D3D12_RASTERIZER_DESC& r) {
			D3D12_RASTERIZER_DESC2 out = {};
			out.FillMode = r.FillMode;
			out.CullMode = r.CullMode;
			out.FrontCounterClockwise = r.FrontCounterClockwise;
			out.DepthBias = (FLOAT)r.DepthBias;
			out.DepthBiasClamp = r.DepthBiasClamp;
			out.SlopeScaledDepthBias = r.SlopeScaledDepthBias;
			out.DepthClipEnable = r.DepthClipEnable;
			out.LineRasterizationMode = r.MultisampleEnable ? D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE
				: r.AntialiasedLineEnable ? D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED : D3D12_LINE_RASTERIZATION_MODE_ALIASED;
			out.ForcedSampleCount = r.ForcedSampleCount;
			out.ConservativeRaster = r.ConservativeRaster;
			return out;
		}

		D3D12_RASTERIZER_DESC2 Rasterizer2(const D3D12_RASTERIZER_DESC1& r) {
			D3D12_RASTERIZER_DESC2 out = {};
			out.FillMode = r.FillMode;
			out.CullMode = r.CullMode;
			out.FrontCounterClockwise = r.FrontCounterClockwise;
			out.DepthBias = r.DepthBias;
			out.DepthBiasClamp = r.DepthBiasClamp;
			out.SlopeScaledDepthBias = r.SlopeScaledDepthBias;
			out.DepthClipEnable = r.DepthClipEnable;
			out.LineRasterizationMode = r.MultisampleEnable ? D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE
				: r.AntialiasedLineEnable ? D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED : D3D12_LINE_RASTERIZATION_MODE_ALIASED;
			out.ForcedSampleCount = r.ForcedSampleCount;
			out.ConservativeRaster = r.ConservativeRaster;
			return out;
		}

		D3D12_DEPTH_STENCILOP_DESC1 StencilOp1(const D3D12_DEPTH_STENCILOP_DESC& op, UINT8 read, UINT8 write) {
			return {op.StencilFailOp, op.StencilDepthFailOp, op.StencilPassOp, op.StencilFunc, read, write};
		}

		D3D12_DEPTH_STENCIL_DESC2 DepthStencil2(const D3D12_DEPTH_STENCIL_DESC& d) {
			D3D12_DEPTH_STENCIL_DESC2 out = {};
			out.DepthEnable = d.DepthEnable;
			out.DepthWriteMask = d.DepthWriteMask;
			out.DepthFunc = d.DepthFunc;
			out.StencilEnable = d.StencilEnable;
			out.FrontFace = StencilOp1(d.FrontFace, d.StencilReadMask, d.StencilWriteMask);
			out.BackFace = StencilOp1(d.BackFace, d.StencilReadMask, d.StencilWriteMask);
			return out;
		}

		D3D12_DEPTH_STENCIL_DESC2 DepthStencil2(const D3D12_DEPTH_STENCIL_DESC1& d) {
			D3D12_DEPTH_STENCIL_DESC2 out = {};
			out.DepthEnable = d.DepthEnable;
			out.DepthWriteMask = d.DepthWriteMask;
			out.DepthFunc = d.DepthFunc;
			out.StencilEnable = d.StencilEnable;
			out.FrontFace = StencilOp1(d.FrontFace, d.StencilReadMask, d.StencilWriteMask);
			out.BackFace = StencilOp1(d.BackFace, d.StencilReadMask, d.StencilWriteMask);
			out.DepthBoundsTestEnable = d.DepthBoundsTestEnable;
			return out;
		}

		// What a stream leaves out takes the API's defaults.
		void PipelineDefaults(PipelineCreate& p) {
			p.sampleMask = UINT_MAX;
			p.rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
			p.rasterizer.CullMode = D3D12_CULL_MODE_BACK;
			p.rasterizer.DepthClipEnable = TRUE;
			for (auto& target : p.blend.RenderTarget) {
				target.SrcBlend = target.SrcBlendAlpha = D3D12_BLEND_ONE;
				target.DestBlend = target.DestBlendAlpha = D3D12_BLEND_ZERO;
				target.BlendOp = target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
				target.LogicOp = D3D12_LOGIC_OP_NOOP;
				target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}
			p.depthStencil.DepthEnable = TRUE;
			p.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			p.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
			const D3D12_DEPTH_STENCILOP_DESC1 op = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS, 0xFF, 0xFF};
			p.depthStencil.FrontFace = p.depthStencil.BackFace = op;
			p.sampleDesc = {1, 0};
		}

		template <class T> const T& At(const u8* stream, size_t& offset) {
			const size_t align = alignof(T) > 4 ? alignof(T) : 4;
			offset = (size_t)AlignUp(offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE), align);
			const T& value = *(const T*)(stream + offset);
			offset = (size_t)AlignUp(offset + sizeof(T), sizeof(void*));
			return value;
		}

		// A pipeline state stream as the superset record; false for a subobject it does not know.
		bool ParseStream(const D3D12_PIPELINE_STATE_STREAM_DESC& desc, PipelineCreate& p, std::string& unknown) {
			PipelineDefaults(p);
			p.fromStream = 1;
			const u8* stream = (const u8*)desc.pPipelineStateSubobjectStream;
			size_t offset = 0;
			while (offset + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) <= desc.SizeInBytes) {
				const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = *(const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*)(stream + offset);
				switch (type) {
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: p.rootSignature = At<ID3D12RootSignature*>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: p.vs = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: p.ps = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: p.ds = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: p.hs = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: p.gs = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: p.cs = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: p.as = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: p.ms = At<D3D12_SHADER_BYTECODE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: p.streamOutput = At<D3D12_STREAM_OUTPUT_DESC>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: p.blend = At<D3D12_BLEND_DESC>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: p.sampleMask = At<UINT>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
						p.rasterizer = Rasterizer2(At<D3D12_RASTERIZER_DESC>(stream, offset));
						p.rasterizerVersion = 0;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
						p.rasterizer = Rasterizer2(At<D3D12_RASTERIZER_DESC1>(stream, offset));
						p.rasterizerVersion = 1;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
						p.rasterizer = At<D3D12_RASTERIZER_DESC2>(stream, offset);
						p.rasterizerVersion = 2;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
						p.depthStencil = DepthStencil2(At<D3D12_DEPTH_STENCIL_DESC>(stream, offset));
						p.depthStencilVersion = 0;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
						p.depthStencil = DepthStencil2(At<D3D12_DEPTH_STENCIL_DESC1>(stream, offset));
						p.depthStencilVersion = 1;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
						p.depthStencil = At<D3D12_DEPTH_STENCIL_DESC2>(stream, offset);
						p.depthStencilVersion = 2;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: p.inputLayout = At<D3D12_INPUT_LAYOUT_DESC>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: p.stripCut = At<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: p.topologyType = At<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: p.renderTargets = At<D3D12_RT_FORMAT_ARRAY>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: p.depthStencilFormat = At<DXGI_FORMAT>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: p.sampleDesc = At<DXGI_SAMPLE_DESC>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: p.nodeMask = At<UINT>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: At<D3D12_CACHED_PIPELINE_STATE>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: p.flags = At<D3D12_PIPELINE_STATE_FLAGS>(stream, offset); break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: p.viewInstancing = At<D3D12_VIEW_INSTANCING_DESC>(stream, offset); break;
					default:
						unknown = Format("pipeline state subobject %d", (int)type);
						return false;
				}
			}
			p.kind = p.cs.pShaderBytecode ? PipelineKind::Compute : p.ms.pShaderBytecode ? PipelineKind::Mesh : PipelineKind::Graphics;
			return true;
		}

		D3D12_SAMPLER_DESC2 Sampler2(const D3D12_SAMPLER_DESC& s) {
			D3D12_SAMPLER_DESC2 out = {};
			out.Filter = s.Filter;
			out.AddressU = s.AddressU;
			out.AddressV = s.AddressV;
			out.AddressW = s.AddressW;
			out.MipLODBias = s.MipLODBias;
			out.MaxAnisotropy = s.MaxAnisotropy;
			out.ComparisonFunc = s.ComparisonFunc;
			memcpy(out.FloatBorderColor, s.BorderColor, sizeof(out.FloatBorderColor));
			out.MinLOD = s.MinLOD;
			out.MaxLOD = s.MaxLOD;
			return out;
		}
	}

	// --- Device ---------------------------------------------------------------------------------------------------------

	WrappedDevice::WrappedDevice(ID3D12Device* real)
		: tracker_(real->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), real->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
			real->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV), real->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)),
		  frames_(*this) {
		void** slots[] = {(void**)&real0_, (void**)&real1_, (void**)&real2_, (void**)&real3_, (void**)&real4_, (void**)&real5_, (void**)&real6_,
			(void**)&real7_, (void**)&real8_, (void**)&real9_, (void**)&real10_, (void**)&real11_, (void**)&real12_, (void**)&real13_, (void**)&real14_};
		for (int v = 0; v < kVersions; ++v)
			if (FAILED(real->QueryInterface(VersionIid(v), slots[v]))) *slots[v] = nullptr;
	}

	WrappedDevice::~WrappedDevice() {
		{
			std::lock_guard<std::mutex> guard(RegistryLock());
			Registry().erase(real0_);
		}
		for (int v = kVersions - 1; v >= 0; --v)
			if (IUnknown* versioned = (IUnknown*)RealVersion(v)) versioned->Release();
	}

	WrappedDevice* WrappedDevice::For(ID3D12Device* real) {
		std::lock_guard<std::mutex> guard(RegistryLock());
		auto found = Registry().find(real);
		if (found != Registry().end()) {
			found->second->AddRef();
			return found->second;
		}
		WrappedDevice* device = new WrappedDevice(real);
		Registry()[real] = device;
		return device;
	}

	std::vector<WrappedDevice*> Devices() {
		std::lock_guard<std::mutex> guard(RegistryLock());
		std::vector<WrappedDevice*> devices;
		for (auto& entry : Registry()) devices.push_back(entry.second);
		return devices;
	}

	ULONG WrappedDevice::Release() {
		const ULONG left = --references_;
		if (left == 0) delete this;
		return left;
	}

	HRESULT WrappedDevice::QueryInterface(REFIID riid, void** out) {
		if (!out) return E_POINTER;
		if (riid == kDeviceProxyIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object)) {
			*out = static_cast<ID3D12Device14*>(this);
			AddRef();
			return S_OK;
		}
		for (int v = 0; v < kVersions; ++v) {
			if (riid == VersionIid(v)) {
				if (!RealVersion(v)) return E_NOINTERFACE;
				*out = static_cast<ID3D12Device14*>(this);
				AddRef();
				return S_OK;
			}
		}
		return real0_->QueryInterface(riid, out);
	}

	std::vector<WrappedQueue*> WrappedDevice::Queues() {
		std::lock_guard<std::mutex> guard(queuesLock_);
		return queues_;
	}

	void WrappedDevice::AddQueue(WrappedQueue* queue) {
		std::lock_guard<std::mutex> guard(queuesLock_);
		queues_.push_back(queue);
	}

	void WrappedDevice::RemoveQueue(WrappedQueue* queue) {
		std::lock_guard<std::mutex> guard(queuesLock_);
		queues_.erase(std::remove(queues_.begin(), queues_.end(), queue), queues_.end());
	}

	void WrappedDevice::Submitted(const std::vector<WrappedList*>& lists) {
		std::unique_lock<std::shared_mutex> guard(tracker_.lock);
		for (WrappedList* list : lists) {
			if (!list) continue;
			for (const ListRecording::Transition& transition : list->Recording().transitions)
				if (transition.resource->alive && transition.resource->resource) transition.resource->resource->SetState(transition.subresource, transition.after);
		}
	}

	void WrappedDevice::Warn(const std::string& text) { frames_.Warn(text); }

	template <class I> static HRESULT Hand(I* object, REFIID riid, void** out) {
		if (!out) {
			object->Release();
			return S_FALSE;
		}
		const HRESULT hr = object->QueryInterface(riid, out);
		object->Release();
		return hr;
	}

	HRESULT WrappedDevice::CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) {
		ID3D12CommandQueue* real = nullptr;
		const HRESULT hr = real0_->CreateCommandQueue(pDesc, __uuidof(ID3D12CommandQueue), (void**)&real);
		if (FAILED(hr)) return hr;
		WrappedQueue* queue = new WrappedQueue(this, real);
		real->Release();
		return Hand(queue, riid, ppCommandQueue);
	}

	HRESULT WrappedDevice::CreateCommandQueue1(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID CreatorID, REFIID riid, void** ppCommandQueue) {
		ID3D12CommandQueue* real = nullptr;
		const HRESULT hr = real9_->CreateCommandQueue1(pDesc, CreatorID, __uuidof(ID3D12CommandQueue), (void**)&real);
		if (FAILED(hr)) return hr;
		WrappedQueue* queue = new WrappedQueue(this, real);
		real->Release();
		return Hand(queue, riid, ppCommandQueue);
	}

	HRESULT WrappedDevice::CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) {
		ID3D12CommandList* real = nullptr;
		const HRESULT hr = real0_->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, __uuidof(ID3D12CommandList), (void**)&real);
		if (FAILED(hr)) return hr;
		WrappedList* list = new WrappedList(this, real, type);
		real->Release();
		list->Opened(pCommandAllocator, pInitialState);
		return Hand(list, riid, ppCommandList);
	}

	HRESULT WrappedDevice::CreateCommandList1(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void** ppCommandList) {
		ID3D12CommandList* real = nullptr;
		const HRESULT hr = real4_->CreateCommandList1(nodeMask, type, flags, __uuidof(ID3D12CommandList), (void**)&real);
		if (FAILED(hr)) return hr;
		WrappedList* list = new WrappedList(this, real, type);
		real->Release();
		return Hand(list, riid, ppCommandList);
	}

	void WrappedDevice::AddPipeline(ID3D12PipelineState* pipeline, PipelineCreate& record) {
		Object* object = tracker_.Add(pipeline, ObjectKind::PipelineState, kCreatePipelineState, record);
		object->named = pipeline;
		WatchLifetime(pipeline, tracker_, object->id);
		frames_.Created(object);
	}

	HRESULT WrappedDevice::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
		ID3D12PipelineState* pipeline = nullptr;
		const HRESULT hr = real0_->CreateGraphicsPipelineState(pDesc, __uuidof(ID3D12PipelineState), (void**)&pipeline);
		if (FAILED(hr)) return hr;
		PipelineCreate p = {};
		p.kind = PipelineKind::Graphics;
		p.rootSignature = pDesc->pRootSignature;
		p.vs = pDesc->VS;
		p.ps = pDesc->PS;
		p.ds = pDesc->DS;
		p.hs = pDesc->HS;
		p.gs = pDesc->GS;
		p.streamOutput = pDesc->StreamOutput;
		p.blend = pDesc->BlendState;
		p.sampleMask = pDesc->SampleMask;
		p.rasterizer = Rasterizer2(pDesc->RasterizerState);
		p.depthStencil = DepthStencil2(pDesc->DepthStencilState);
		p.inputLayout = pDesc->InputLayout;
		p.stripCut = pDesc->IBStripCutValue;
		p.topologyType = pDesc->PrimitiveTopologyType;
		p.renderTargets.NumRenderTargets = pDesc->NumRenderTargets;
		memcpy(p.renderTargets.RTFormats, pDesc->RTVFormats, sizeof(p.renderTargets.RTFormats));
		p.depthStencilFormat = pDesc->DSVFormat;
		p.sampleDesc = pDesc->SampleDesc;
		p.nodeMask = pDesc->NodeMask;
		p.flags = pDesc->Flags;
		AddPipeline(pipeline, p);
		return Hand(pipeline, riid, ppPipelineState);
	}

	HRESULT WrappedDevice::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
		ID3D12PipelineState* pipeline = nullptr;
		const HRESULT hr = real0_->CreateComputePipelineState(pDesc, __uuidof(ID3D12PipelineState), (void**)&pipeline);
		if (FAILED(hr)) return hr;
		PipelineCreate p = {};
		PipelineDefaults(p);
		p.kind = PipelineKind::Compute;
		p.rootSignature = pDesc->pRootSignature;
		p.cs = pDesc->CS;
		p.nodeMask = pDesc->NodeMask;
		p.flags = pDesc->Flags;
		AddPipeline(pipeline, p);
		return Hand(pipeline, riid, ppPipelineState);
	}

	HRESULT WrappedDevice::CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc, REFIID riid, void** ppPipelineState) {
		ID3D12PipelineState* pipeline = nullptr;
		const HRESULT hr = real2_->CreatePipelineState(pDesc, __uuidof(ID3D12PipelineState), (void**)&pipeline);
		if (FAILED(hr)) return hr;
		PipelineCreate p = {};
		std::string unknown;
		if (ParseStream(*pDesc, p, unknown)) AddPipeline(pipeline, p);
		else {
			Log("a pipeline state with %s is not recorded", unknown.c_str());
			Object* object = tracker_.Register(pipeline, ObjectKind::PipelineState);
			object->named = pipeline;
			WatchLifetime(pipeline, tracker_, object->id);
		}
		return Hand(pipeline, riid, ppPipelineState);
	}

	HRESULT WrappedDevice::CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) {
		ID3D12DescriptorHeap* heap = nullptr;
		const HRESULT hr = real0_->CreateDescriptorHeap(pDescriptorHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&heap);
		if (FAILED(hr)) return hr;
		DescriptorHeapCreate record = {};
		record.desc = *pDescriptorHeapDesc;
		record.cpuStart = heap->GetCPUDescriptorHandleForHeapStart().ptr;
		record.gpuStart = (pDescriptorHeapDesc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ? heap->GetGPUDescriptorHandleForHeapStart().ptr : 0;
		record.increment = tracker_.Increment(pDescriptorHeapDesc->Type);
		Object* object = tracker_.Add(heap, ObjectKind::DescriptorHeap, kCreateDescriptorHeap, record);
		object->named = heap;
		object->heap = std::make_unique<DescriptorHeapTrack>();
		object->heap->desc = record.desc;
		object->heap->cpuStart = record.cpuStart;
		object->heap->gpuStart = record.gpuStart;
		object->heap->increment = record.increment;
		tracker_.AddHeapRanges(object);
		WatchLifetime(heap, tracker_, object->id);
		frames_.Created(object);
		return Hand(heap, riid, ppvHeap);
	}

	HRESULT WrappedDevice::CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) {
		ID3D12RootSignature* signature = nullptr;
		const HRESULT hr = real0_->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, __uuidof(ID3D12RootSignature), (void**)&signature);
		if (FAILED(hr)) return hr;
		// The runtime gives back the same object for the same blob; record it once.
		if (!tracker_.Find(signature)) {
			RootSignatureCreate record = {0, nodeMask, pBlobWithRootSignature, (u64)blobLengthInBytes};
			Object* object = tracker_.Add(signature, ObjectKind::RootSignature, kCreateRootSignature, record);
			object->named = signature;
			WatchLifetime(signature, tracker_, object->id);
			frames_.Created(object);
		}
		return Hand(signature, riid, ppvRootSignature);
	}

	void WrappedDevice::View(ViewKind kind, ID3D12Resource* resource, ID3D12Resource* counter, const void* desc, size_t descSize, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
		DescriptorWrite write = {};
		write.kind = kind;
		write.resource = resource;
		write.counter = counter;
		write.hasDesc = desc ? 1 : 0;
		if (desc) memcpy(&write.cbv, desc, descSize);
		const std::pair<u64, u32> slot = tracker_.WriteDescriptor(handle.ptr, write);
		if (!slot.first) return;
		write.heap = slot.first;
		write.index = slot.second;
		frames_.DescriptorWritten(write);
	}

	void WrappedDevice::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateConstantBufferView(pDesc, DestDescriptor);
		View(ViewKind::Cbv, nullptr, nullptr, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
		View(ViewKind::Srv, pResource, nullptr, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
		View(ViewKind::Uav, pResource, pCounterResource, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
		View(ViewKind::Rtv, pResource, nullptr, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
		View(ViewKind::Dsv, pResource, nullptr, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real0_->CreateSampler(pDesc, DestDescriptor);
		const D3D12_SAMPLER_DESC2 desc = Sampler2(*pDesc);
		View(ViewKind::Sampler, nullptr, nullptr, &desc, sizeof(desc), DestDescriptor);
	}

	void WrappedDevice::CreateSampler2(const D3D12_SAMPLER_DESC2* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
		real11_->CreateSampler2(pDesc, DestDescriptor);
		View(ViewKind::Sampler2, nullptr, nullptr, pDesc, sizeof(*pDesc), DestDescriptor);
	}

	void WrappedDevice::CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {
		real0_->CopyDescriptors(NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);
		// The descriptors stream from the source ranges into the destination ranges, each side at its own pace.
		const u32 step = tracker_.Increment(DescriptorHeapsType);
		std::vector<DescriptorCopy> runs;
		UINT dstRange = 0, srcRange = 0, dstAt = 0, srcAt = 0;
		while (dstRange < NumDestDescriptorRanges && srcRange < NumSrcDescriptorRanges) {
			const UINT dstSize = pDestDescriptorRangeSizes ? pDestDescriptorRangeSizes[dstRange] : 1;
			const UINT srcSize = pSrcDescriptorRangeSizes ? pSrcDescriptorRangeSizes[srcRange] : 1;
			const UINT run = std::min(dstSize - dstAt, srcSize - srcAt);
			if (run) tracker_.CopyDescriptors(pDestDescriptorRangeStarts[dstRange].ptr + (SIZE_T)dstAt * step, pSrcDescriptorRangeStarts[srcRange].ptr + (SIZE_T)srcAt * step, run, DescriptorHeapsType, &runs);
			dstAt += run;
			srcAt += run;
			if (dstAt >= dstSize) {
				++dstRange;
				dstAt = 0;
			}
			if (srcAt >= srcSize) {
				++srcRange;
				srcAt = 0;
			}
		}
		frames_.DescriptorsCopied(runs);
	}

	void WrappedDevice::CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {
		real0_->CopyDescriptorsSimple(NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
		std::vector<DescriptorCopy> runs;
		tracker_.CopyDescriptors(DestDescriptorRangeStart.ptr, SrcDescriptorRangeStart.ptr, NumDescriptors, DescriptorHeapsType, &runs);
		frames_.DescriptorsCopied(runs);
	}

	Object* WrappedDevice::AddResource(ID3D12Resource* resource, ResourceCreate& record, const D3D12_RESOURCE_DESC1& desc, D3D12_HEAP_TYPE heapType, D3D12_CPU_PAGE_PROPERTY cpuPage, D3D12_RESOURCE_STATES state) {
		record.desc = desc;
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) record.gpuAddress = resource->GetGPUVirtualAddress();
		Object* object = tracker_.Add(resource, ObjectKind::Resource, kCreateResource, record);
		object->named = resource;
		auto track = std::make_unique<ResourceTrack>();
		track->kind = record.kind;
		track->desc = desc;
		track->heapType = heapType;
		track->cpuPage = cpuPage;
		track->gpuAddress = record.gpuAddress;
		track->subresources = SubresourceCount(real0_, desc);
		track->states.assign(1, state);
		if (record.useLayout) {
			track->usesLayouts = true;
			track->layouts.assign(1, record.initialLayout);
		}
		{
			std::unique_lock<std::shared_mutex> guard(tracker_.lock);
			object->resource = std::move(track);
		}
		tracker_.AddAddressRange(object);
		WatchLifetime(resource, tracker_, object->id);
		frames_.Created(object);
		return object;
	}

	// The state a resource of `heap` starts in: upload heaps are always GENERIC_READ, readback heaps COPY_DEST.
	static D3D12_RESOURCE_STATES StartState(D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
		if (heap == D3D12_HEAP_TYPE_UPLOAD) return D3D12_RESOURCE_STATE_GENERIC_READ;
		if (heap == D3D12_HEAP_TYPE_READBACK) return D3D12_RESOURCE_STATE_COPY_DEST;
		return state;
	}

	HRESULT WrappedDevice::CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real0_->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		ResourceCreate record = {};
		record.kind = ResourceKind::Committed;
		record.heapProperties = *pHeapProperties;
		record.heapFlags = HeapFlags;
		record.initialState = InitialResourceState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, ToDesc1(*pDesc), pHeapProperties->Type, pHeapProperties->CPUPageProperty, StartState(pHeapProperties->Type, InitialResourceState));
		return Hand(resource, riidResource, ppvResource);
	}

	HRESULT WrappedDevice::CreateCommittedResource1(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riidResource, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real4_->CreateCommittedResource1(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, pProtectedSession, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		ResourceCreate record = {};
		record.kind = ResourceKind::Committed;
		record.heapProperties = *pHeapProperties;
		record.heapFlags = HeapFlags;
		record.initialState = InitialResourceState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, ToDesc1(*pDesc), pHeapProperties->Type, pHeapProperties->CPUPageProperty, StartState(pHeapProperties->Type, InitialResourceState));
		return Hand(resource, riidResource, ppvResource);
	}

	HRESULT WrappedDevice::CreateCommittedResource2(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riidResource, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real8_->CreateCommittedResource2(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, pProtectedSession, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		ResourceCreate record = {};
		record.kind = ResourceKind::Committed;
		record.heapProperties = *pHeapProperties;
		record.heapFlags = HeapFlags;
		record.initialState = InitialResourceState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, *pDesc, pHeapProperties->Type, pHeapProperties->CPUPageProperty, StartState(pHeapProperties->Type, InitialResourceState));
		return Hand(resource, riidResource, ppvResource);
	}

	HRESULT WrappedDevice::CreateCommittedResource3(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1* pDesc, D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats, REFIID riidResource, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real10_->CreateCommittedResource3(pHeapProperties, HeapFlags, pDesc, InitialLayout, pOptimizedClearValue, pProtectedSession, NumCastableFormats, pCastableFormats, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		ResourceCreate record = {};
		record.kind = ResourceKind::Committed;
		record.heapProperties = *pHeapProperties;
		record.heapFlags = HeapFlags;
		record.useLayout = 1;
		record.initialLayout = InitialLayout;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		record.castableFormatCount = NumCastableFormats;
		record.castableFormats = pCastableFormats;
		AddResource(resource, record, *pDesc, pHeapProperties->Type, pHeapProperties->CPUPageProperty, StartState(pHeapProperties->Type, D3D12_RESOURCE_STATE_COMMON));
		return Hand(resource, riidResource, ppvResource);
	}

	HRESULT WrappedDevice::CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) {
		ID3D12Heap* heap = nullptr;
		const HRESULT hr = real0_->CreateHeap(pDesc, __uuidof(ID3D12Heap), (void**)&heap);
		if (FAILED(hr)) return hr;
		HeapCreate record = {0, *pDesc};
		Object* object = tracker_.Add(heap, ObjectKind::Heap, kCreateHeap, record);
		object->named = heap;
		WatchLifetime(heap, tracker_, object->id);
		frames_.Created(object);
		return Hand(heap, riid, ppvHeap);
	}

	HRESULT WrappedDevice::CreateHeap1(const D3D12_HEAP_DESC* pDesc, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riid, void** ppvHeap) {
		ID3D12Heap* heap = nullptr;
		const HRESULT hr = real4_->CreateHeap1(pDesc, pProtectedSession, __uuidof(ID3D12Heap), (void**)&heap);
		if (FAILED(hr)) return hr;
		HeapCreate record = {0, *pDesc};
		Object* object = tracker_.Add(heap, ObjectKind::Heap, kCreateHeap, record);
		object->named = heap;
		WatchLifetime(heap, tracker_, object->id);
		frames_.Created(object);
		return Hand(heap, riid, ppvHeap);
	}

	static ResourceCreate Placed(ID3D12Heap* heap, UINT64 offset, Tracker& tracker, D3D12_HEAP_DESC& heapDesc) {
		ResourceCreate record = {};
		record.kind = ResourceKind::Placed;
		heapDesc = heap->GetDesc();
		record.heapProperties = heapDesc.Properties;
		record.heapFlags = heapDesc.Flags;
		record.heap = tracker.Id(heap);
		record.heapOffset = offset;
		return record;
	}

	HRESULT WrappedDevice::CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real0_->CreatePlacedResource(pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		D3D12_HEAP_DESC heapDesc;
		ResourceCreate record = Placed(pHeap, HeapOffset, tracker_, heapDesc);
		record.initialState = InitialState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, ToDesc1(*pDesc), heapDesc.Properties.Type, heapDesc.Properties.CPUPageProperty, StartState(heapDesc.Properties.Type, InitialState));
		return Hand(resource, riid, ppvResource);
	}

	HRESULT WrappedDevice::CreatePlacedResource1(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real8_->CreatePlacedResource1(pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		D3D12_HEAP_DESC heapDesc;
		ResourceCreate record = Placed(pHeap, HeapOffset, tracker_, heapDesc);
		record.initialState = InitialState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, *pDesc, heapDesc.Properties.Type, heapDesc.Properties.CPUPageProperty, StartState(heapDesc.Properties.Type, InitialState));
		return Hand(resource, riid, ppvResource);
	}

	HRESULT WrappedDevice::CreatePlacedResource2(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1* pDesc, D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE* pOptimizedClearValue, UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats, REFIID riid, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real10_->CreatePlacedResource2(pHeap, HeapOffset, pDesc, InitialLayout, pOptimizedClearValue, NumCastableFormats, pCastableFormats, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		D3D12_HEAP_DESC heapDesc;
		ResourceCreate record = Placed(pHeap, HeapOffset, tracker_, heapDesc);
		record.useLayout = 1;
		record.initialLayout = InitialLayout;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		record.castableFormatCount = NumCastableFormats;
		record.castableFormats = pCastableFormats;
		AddResource(resource, record, *pDesc, heapDesc.Properties.Type, heapDesc.Properties.CPUPageProperty, StartState(heapDesc.Properties.Type, D3D12_RESOURCE_STATE_COMMON));
		return Hand(resource, riid, ppvResource);
	}

	HRESULT WrappedDevice::CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
		ID3D12Resource* resource = nullptr;
		const HRESULT hr = real0_->CreateReservedResource(pDesc, InitialState, pOptimizedClearValue, __uuidof(ID3D12Resource), (void**)&resource);
		if (FAILED(hr)) return hr;
		ResourceCreate record = {};
		record.kind = ResourceKind::Reserved;
		record.heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		record.initialState = InitialState;
		record.hasClearValue = pOptimizedClearValue ? 1 : 0;
		if (pOptimizedClearValue) record.clearValue = *pOptimizedClearValue;
		AddResource(resource, record, ToDesc1(*pDesc), D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, InitialState);
		return Hand(resource, riid, ppvResource);
	}

	HRESULT WrappedDevice::CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) {
		ID3D12Fence* fence = nullptr;
		const HRESULT hr = real0_->CreateFence(InitialValue, Flags, __uuidof(ID3D12Fence), (void**)&fence);
		if (FAILED(hr)) return hr;
		FenceCreate record = {0, InitialValue, Flags};
		Object* object = tracker_.Add(fence, ObjectKind::Fence, kCreateFence, record);
		object->named = fence;
		WatchLifetime(fence, tracker_, object->id);
		frames_.Created(object);
		return Hand(fence, riid, ppFence);
	}

	HRESULT WrappedDevice::CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) {
		ID3D12QueryHeap* heap = nullptr;
		const HRESULT hr = real0_->CreateQueryHeap(pDesc, __uuidof(ID3D12QueryHeap), (void**)&heap);
		if (FAILED(hr)) return hr;
		QueryHeapCreate record = {0, *pDesc};
		Object* object = tracker_.Add(heap, ObjectKind::QueryHeap, kCreateQueryHeap, record);
		object->named = heap;
		WatchLifetime(heap, tracker_, object->id);
		frames_.Created(object);
		return Hand(heap, riid, ppvHeap);
	}

	HRESULT WrappedDevice::CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppvCommandSignature) {
		ID3D12CommandSignature* signature = nullptr;
		const HRESULT hr = real0_->CreateCommandSignature(pDesc, pRootSignature, __uuidof(ID3D12CommandSignature), (void**)&signature);
		if (FAILED(hr)) return hr;
		CommandSignatureCreate record = {0, *pDesc, pRootSignature};
		Object* object = tracker_.Add(signature, ObjectKind::CommandSignature, kCreateCommandSignature, record);
		object->named = signature;
		WatchLifetime(signature, tracker_, object->id);
		frames_.Created(object);
		return Hand(signature, riid, ppvCommandSignature);
	}

	HRESULT WrappedDevice::CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) {
		Log("pipeline libraries are not captured yet: pipelines loaded from one replay without their state");
		return real1_->CreatePipelineLibrary(pLibraryBlob, BlobLength, riid, ppPipelineLibrary);
	}

	HRESULT WrappedDevice::CreateStateObject(const D3D12_STATE_OBJECT_DESC* pDesc, REFIID riid, void** ppStateObject) {
		Log("state objects (ray tracing, work graphs) are not captured yet");
		return real5_->CreateStateObject(pDesc, riid, ppStateObject);
	}

	// --- Command queue ---------------------------------------------------------------------------------------------------

	// {4D8E2F63-7A9B-4C01-B2C3-D4E5F6071829}
	const GUID kQueueProxyIid = {0x4d8e2f63, 0x7a9b, 0x4c01, {0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, 0x29}};

	WrappedQueue::WrappedQueue(WrappedDevice* device, ID3D12CommandQueue* real) : device_(device) {
		device_->AddRef();
		real->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&real0_);
		QueueCreate record = {0, real0_->GetDesc()};
		Object* object = device_->Objects().Add(this, ObjectKind::CommandQueue, kCreateCommandQueue, record);
		object->named = real0_;
		id_ = object->id;
		device_->AddQueue(this);
		device_->Frames().Created(object);
	}

	WrappedQueue::~WrappedQueue() {
		device_->RemoveQueue(this);
		device_->Objects().Destroyed(id_);
		real0_->Release();
		device_->Release();
	}

	ULONG WrappedQueue::Release() {
		const ULONG left = --references_;
		if (left == 0) delete this;
		return left;
	}

	HRESULT WrappedQueue::QueryInterface(REFIID riid, void** out) {
		if (!out) return E_POINTER;
		if (riid == kQueueProxyIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
			riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12CommandQueue)) {
			*out = static_cast<ID3D12CommandQueue*>(this);
			AddRef();
			return S_OK;
		}
		return real0_->QueryInterface(riid, out);
	}

	HRESULT WrappedQueue::GetDevice(REFIID riid, void** ppvDevice) { return device_->QueryInterface(riid, ppvDevice); }

	void WrappedQueue::ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
		std::vector<ID3D12CommandList*> reals(NumCommandLists);
		std::vector<WrappedList*> lists(NumCommandLists, nullptr);
		for (UINT i = 0; i < NumCommandLists; ++i) {
			WrappedList* list = nullptr;
			if (ppCommandLists[i] && SUCCEEDED(ppCommandLists[i]->QueryInterface(kListProxyIid, (void**)&list))) {
				lists[i] = list;
				reals[i] = list->Real();
				list->Release();
			}
			else reals[i] = ppCommandLists[i];
		}
		device_->Frames().BeforeExecute(this, lists);
		real0_->ExecuteCommandLists(NumCommandLists, reals.data());
		device_->Submitted(lists);
	}

	HRESULT WrappedQueue::Signal(ID3D12Fence* pFence, UINT64 Value) {
		const HRESULT hr = real0_->Signal(pFence, Value);
		device_->Frames().QueueFence(kQueueSignal, this, pFence, Value);
		return hr;
	}

	HRESULT WrappedQueue::Wait(ID3D12Fence* pFence, UINT64 Value) {
		const HRESULT hr = real0_->Wait(pFence, Value);
		device_->Frames().QueueFence(kQueueWait, this, pFence, Value);
		return hr;
	}

	void WrappedQueue::SetMarker(UINT Metadata, const void* pData, UINT Size) {
		real0_->SetMarker(Metadata, pData, Size);
		device_->Frames().QueueMarker(this, MarkerOp::Set, Metadata, pData, pData ? Size : 0);
	}

	void WrappedQueue::BeginEvent(UINT Metadata, const void* pData, UINT Size) {
		real0_->BeginEvent(Metadata, pData, Size);
		device_->Frames().QueueMarker(this, MarkerOp::Begin, Metadata, pData, pData ? Size : 0);
	}

	void WrappedQueue::EndEvent(void) {
		real0_->EndEvent();
		device_->Frames().QueueMarker(this, MarkerOp::End, 0, nullptr, 0);
	}

	// --- Entry points -------------------------------------------------------------------------------------------------

	HRESULT WrapDevice(IUnknown* device, REFIID riid, void** out) {
		if (!device || !out) return E_POINTER;
		WrappedDevice* proxy = nullptr;
		if (SUCCEEDED(device->QueryInterface(kDeviceProxyIid, (void**)&proxy))) {
			const HRESULT hr = proxy->QueryInterface(riid, out);
			proxy->Release();
			return hr;
		}
		ID3D12Device* real = nullptr;
		if (FAILED(device->QueryInterface(__uuidof(ID3D12Device), (void**)&real))) return E_NOINTERFACE;
		proxy = WrappedDevice::For(real);
		real->Release();
		const HRESULT hr = proxy->QueryInterface(riid, out);
		proxy->Release();
		Session::Get().NoteApi("D3D12");
		return hr;
	}
}
