// The command list proxy: every call goes to the runtime's list, then into the list's recording (docs/22 "Capture").
#include "wrappers.h"

namespace fb::capture::d3d12
{
	// {2C4E6A71-0B3D-4F15-8E92-61A7C3D5F0B2}: QueryInterface with it answers a command list proxy itself.
	const GUID kListProxyIid = {0x2c4e6a71, 0x0b3d, 0x4f15, {0x8e, 0x92, 0x61, 0xa7, 0xc3, 0xd5, 0xf0, 0xb2}};

	WrappedList::WrappedList(WrappedDevice* device, ID3D12CommandList* real, D3D12_COMMAND_LIST_TYPE type) : device_(device) {
		device_->AddRef();
		for (int v = 0; v < kVersions; ++v) {
			void* versioned = nullptr;
			if (FAILED(real->QueryInterface(VersionIid(v), &versioned))) continue;
			switch (v) {
				case 0: real0_ = (ID3D12CommandList*)versioned; break;
				case 1: real1_ = (ID3D12GraphicsCommandList*)versioned; break;
				case 2: real2_ = (ID3D12GraphicsCommandList1*)versioned; break;
				case 3: real3_ = (ID3D12GraphicsCommandList2*)versioned; break;
				case 4: real4_ = (ID3D12GraphicsCommandList3*)versioned; break;
				case 5: real5_ = (ID3D12GraphicsCommandList4*)versioned; break;
				case 6: real6_ = (ID3D12GraphicsCommandList5*)versioned; break;
				case 7: real7_ = (ID3D12GraphicsCommandList6*)versioned; break;
				case 8: real8_ = (ID3D12GraphicsCommandList7*)versioned; break;
				case 9: real9_ = (ID3D12GraphicsCommandList8*)versioned; break;
				case 10: real10_ = (ID3D12GraphicsCommandList9*)versioned; break;
				case 11: real11_ = (ID3D12GraphicsCommandList10*)versioned; break;
			}
		}
		recording_.type = type;
		ListCreate record = {0, type};
		Object* object = device_->Objects().Add(this, ObjectKind::CommandList, kCreateCommandList, record);
		object->named = real0_;
		id_ = object->id;
		device_->Frames().Created(object);
	}

	WrappedList::~WrappedList() {
		device_->Objects().Destroyed(id_);
		for (int v = 0; v < kVersions; ++v)
			if (IUnknown* versioned = (IUnknown*)RealVersion(v)) versioned->Release();
		device_->Release();
	}

	ULONG WrappedList::Release() {
		const ULONG left = --references_;
		if (left == 0) delete this;
		return left;
	}

	HRESULT WrappedList::QueryInterface(REFIID riid, void** out) {
		if (!out) return E_POINTER;
		if (riid == kListProxyIid || riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild)) {
			*out = static_cast<ID3D12GraphicsCommandList10*>(this);
			AddRef();
			return S_OK;
		}
		for (int v = 0; v < kVersions; ++v) {
			if (riid == VersionIid(v)) {
				if (!RealVersion(v)) break;
				*out = static_cast<ID3D12GraphicsCommandList10*>(this);
				AddRef();
				return S_OK;
			}
		}
		return real0_->QueryInterface(riid, out);
	}

	HRESULT WrappedList::GetDevice(REFIID riid, void** ppvDevice) { return device_->QueryInterface(riid, ppvDevice); }

	template <class A> void WrappedList::Record(Cmd command, A& args) {
		WriteCommand(recording_.commands, &device_->Objects(), command, args);
		++recording_.count;
	}

	void WrappedList::Unsupported(const char* method) {
		UnsupportedArgs args = {method};
		Record(Cmd::Unsupported, args);
		for (const std::string& known : recording_.unsupported)
			if (known == method) return;
		recording_.unsupported.push_back(method);
	}

	void WrappedList::Opened(ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState) {
		ResetArgs args = {allocator, initialState};
		Record(Cmd::Reset, args);
	}

	HRESULT WrappedList::Close(void) { return real1_->Close(); }

	HRESULT WrappedList::Reset(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) {
		const HRESULT hr = real1_->Reset(pAllocator, pInitialState);
		recording_.commands.Clear();
		recording_.count = 0;
		recording_.transitions.clear();
		recording_.unsupported.clear();
		recording_.bundles = false;
		ResetArgs args = {pAllocator, pInitialState};
		Record(Cmd::Reset, args);
		return hr;
	}

	void WrappedList::ClearState(ID3D12PipelineState* pPipelineState) {
		real1_->ClearState(pPipelineState);
		PipelineArgs args = {pPipelineState};
		Record(Cmd::ClearState, args);
	}

	void WrappedList::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
		real1_->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
		DrawArgs args = {VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation};
		Record(Cmd::DrawInstanced, args);
	}

	void WrappedList::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) {
		real1_->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
		DrawIndexedArgs args = {IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation};
		Record(Cmd::DrawIndexedInstanced, args);
	}

	void WrappedList::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) {
		real1_->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
		DispatchArgs args = {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ};
		Record(Cmd::Dispatch, args);
	}

	void WrappedList::CopyBufferRegion(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT64 NumBytes) {
		real1_->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);
		CopyBufferRegionArgs args = {pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes};
		Record(Cmd::CopyBufferRegion, args);
	}

	void WrappedList::CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION* pSrc, const D3D12_BOX* pSrcBox) {
		real1_->CopyTextureRegion(pDst, DstX, DstY, DstZ, pSrc, pSrcBox);
		CopyTextureRegionArgs args = {*pDst, DstX, DstY, DstZ, *pSrc, pSrcBox};
		Record(Cmd::CopyTextureRegion, args);
	}

	void WrappedList::CopyResource(ID3D12Resource* pDstResource, ID3D12Resource* pSrcResource) {
		real1_->CopyResource(pDstResource, pSrcResource);
		CopyResourceArgs args = {pDstResource, pSrcResource};
		Record(Cmd::CopyResource, args);
	}

	void WrappedList::CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate, const D3D12_TILE_REGION_SIZE* pTileRegionSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) {
		real1_->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags);
		CopyTilesArgs args = {pTiledResource, *pTileRegionStartCoordinate, *pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags};
		Record(Cmd::CopyTiles, args);
	}

	void WrappedList::ResolveSubresource(ID3D12Resource* pDstResource, UINT DstSubresource, ID3D12Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) {
		real1_->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
		ResolveSubresourceArgs args = {pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format};
		Record(Cmd::ResolveSubresource, args);
	}

	void WrappedList::IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) {
		real1_->IASetPrimitiveTopology(PrimitiveTopology);
		TopologyArgs args = {PrimitiveTopology};
		Record(Cmd::IASetPrimitiveTopology, args);
	}

	void WrappedList::RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT* pViewports) {
		real1_->RSSetViewports(NumViewports, pViewports);
		ViewportsArgs args = {NumViewports, pViewports};
		Record(Cmd::RSSetViewports, args);
	}

	void WrappedList::RSSetScissorRects(UINT NumRects, const D3D12_RECT* pRects) {
		real1_->RSSetScissorRects(NumRects, pRects);
		ScissorsArgs args = {NumRects, pRects};
		Record(Cmd::RSSetScissorRects, args);
	}

	void WrappedList::OMSetBlendFactor(const FLOAT BlendFactor[4]) {
		real1_->OMSetBlendFactor(BlendFactor);
		BlendFactorArgs args = {};
		if (BlendFactor) memcpy(args.factor, BlendFactor, sizeof(args.factor));
		else args.factor[0] = args.factor[1] = args.factor[2] = args.factor[3] = 1.0f;
		Record(Cmd::OMSetBlendFactor, args);
	}

	void WrappedList::OMSetStencilRef(UINT StencilRef) {
		real1_->OMSetStencilRef(StencilRef);
		StencilRefArgs args = {StencilRef};
		Record(Cmd::OMSetStencilRef, args);
	}

	void WrappedList::SetPipelineState(ID3D12PipelineState* pPipelineState) {
		real1_->SetPipelineState(pPipelineState);
		PipelineArgs args = {pPipelineState};
		Record(Cmd::SetPipelineState, args);
	}

	void WrappedList::ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) {
		real1_->ResourceBarrier(NumBarriers, pBarriers);
		BarrierArgs args = {NumBarriers, pBarriers};
		Record(Cmd::ResourceBarrier, args);
		for (UINT i = 0; i < NumBarriers; ++i) {
			const D3D12_RESOURCE_BARRIER& barrier = pBarriers[i];
			if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || (barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)) continue;
			if (Object* resource = device_->Objects().Find(barrier.Transition.pResource))
				recording_.transitions.push_back({resource, barrier.Transition.Subresource, barrier.Transition.StateAfter});
		}
	}

	void WrappedList::ExecuteBundle(ID3D12GraphicsCommandList* pCommandList) {
		WrappedList* bundle = nullptr;
		if (pCommandList && SUCCEEDED(pCommandList->QueryInterface(kListProxyIid, (void**)&bundle))) {
			real1_->ExecuteBundle((ID3D12GraphicsCommandList*)bundle->real1_);
			bundle->Release();
		}
		else real1_->ExecuteBundle(pCommandList);
		BundleArgs args = {pCommandList};
		Record(Cmd::ExecuteBundle, args);
		recording_.bundles = true;
	}

	void WrappedList::SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps) {
		real1_->SetDescriptorHeaps(NumDescriptorHeaps, ppDescriptorHeaps);
		DescriptorHeapsArgs args = {NumDescriptorHeaps, ppDescriptorHeaps};
		Record(Cmd::SetDescriptorHeaps, args);
	}

	void WrappedList::SetComputeRootSignature(ID3D12RootSignature* pRootSignature) {
		real1_->SetComputeRootSignature(pRootSignature);
		RootSignatureArgs args = {pRootSignature};
		Record(Cmd::SetComputeRootSignature, args);
	}

	void WrappedList::SetGraphicsRootSignature(ID3D12RootSignature* pRootSignature) {
		real1_->SetGraphicsRootSignature(pRootSignature);
		RootSignatureArgs args = {pRootSignature};
		Record(Cmd::SetGraphicsRootSignature, args);
	}

	void WrappedList::SetComputeRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
		real1_->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
		RootTableArgs args = {RootParameterIndex, BaseDescriptor};
		Record(Cmd::SetComputeRootDescriptorTable, args);
	}

	void WrappedList::SetGraphicsRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
		real1_->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
		RootTableArgs args = {RootParameterIndex, BaseDescriptor};
		Record(Cmd::SetGraphicsRootDescriptorTable, args);
	}

	void WrappedList::SetComputeRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) {
		real1_->SetComputeRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
		RootConstantArgs args = {RootParameterIndex, SrcData, DestOffsetIn32BitValues};
		Record(Cmd::SetComputeRoot32BitConstant, args);
	}

	void WrappedList::SetGraphicsRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) {
		real1_->SetGraphicsRoot32BitConstant(RootParameterIndex, SrcData, DestOffsetIn32BitValues);
		RootConstantArgs args = {RootParameterIndex, SrcData, DestOffsetIn32BitValues};
		Record(Cmd::SetGraphicsRoot32BitConstant, args);
	}

	void WrappedList::SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) {
		real1_->SetComputeRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
		RootConstantsArgs args = {RootParameterIndex, Num32BitValuesToSet, (const UINT*)pSrcData, DestOffsetIn32BitValues};
		Record(Cmd::SetComputeRoot32BitConstants, args);
	}

	void WrappedList::SetGraphicsRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) {
		real1_->SetGraphicsRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
		RootConstantsArgs args = {RootParameterIndex, Num32BitValuesToSet, (const UINT*)pSrcData, DestOffsetIn32BitValues};
		Record(Cmd::SetGraphicsRoot32BitConstants, args);
	}

#define ROOT_VIEW(Method)                                                                     \
	void WrappedList::Method(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) { \
		real1_->Method(RootParameterIndex, BufferLocation);                                      \
		RootViewArgs args = {RootParameterIndex, BufferLocation};                                \
		Record(Cmd::Method, args);                                                               \
	}
	ROOT_VIEW(SetComputeRootConstantBufferView)
	ROOT_VIEW(SetGraphicsRootConstantBufferView)
	ROOT_VIEW(SetComputeRootShaderResourceView)
	ROOT_VIEW(SetGraphicsRootShaderResourceView)
	ROOT_VIEW(SetComputeRootUnorderedAccessView)
	ROOT_VIEW(SetGraphicsRootUnorderedAccessView)
#undef ROOT_VIEW

	void WrappedList::IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) {
		real1_->IASetIndexBuffer(pView);
		IndexBufferArgs args = {pView};
		Record(Cmd::IASetIndexBuffer, args);
	}

	void WrappedList::IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW* pViews) {
		real1_->IASetVertexBuffers(StartSlot, NumViews, pViews);
		std::vector<D3D12_VERTEX_BUFFER_VIEW> none;
		if (!pViews && NumViews) {
			none.assign(NumViews, D3D12_VERTEX_BUFFER_VIEW{});
			pViews = none.data();
		}
		VertexBuffersArgs args = {StartSlot, NumViews, pViews};
		Record(Cmd::IASetVertexBuffers, args);
	}

	void WrappedList::SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews) {
		real1_->SOSetTargets(StartSlot, NumViews, pViews);
		std::vector<D3D12_STREAM_OUTPUT_BUFFER_VIEW> none;
		if (!pViews && NumViews) {
			none.assign(NumViews, D3D12_STREAM_OUTPUT_BUFFER_VIEW{});
			pViews = none.data();
		}
		SOTargetsArgs args = {StartSlot, NumViews, pViews};
		Record(Cmd::SOSetTargets, args);
	}

	void WrappedList::OMSetRenderTargets(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) {
		real1_->OMSetRenderTargets(NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
		// A single handle naming a range becomes one handle per target, so a replay needs no increment.
		D3D12_CPU_DESCRIPTOR_HANDLE targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
		const UINT count = std::min<UINT>(NumRenderTargetDescriptors, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
		const u32 step = device_->Objects().Increment(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		for (UINT i = 0; i < count && pRenderTargetDescriptors; ++i)
			targets[i].ptr = RTsSingleHandleToDescriptorRange ? pRenderTargetDescriptors[0].ptr + (SIZE_T)i * step : pRenderTargetDescriptors[i].ptr;
		RenderTargetsArgs args = {pRenderTargetDescriptors ? count : 0, targets, pDepthStencilDescriptor};
		Record(Cmd::OMSetRenderTargets, args);
	}

	void WrappedList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT* pRects) {
		real1_->ClearDepthStencilView(DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects);
		ClearDepthStencilArgs args = {DepthStencilView, ClearFlags, Depth, Stencil, pRects ? NumRects : 0, pRects};
		Record(Cmd::ClearDepthStencilView, args);
	}

	void WrappedList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT* pRects) {
		real1_->ClearRenderTargetView(RenderTargetView, ColorRGBA, NumRects, pRects);
		ClearRenderTargetArgs args = {RenderTargetView, {ColorRGBA[0], ColorRGBA[1], ColorRGBA[2], ColorRGBA[3]}, pRects ? NumRects : 0, pRects};
		Record(Cmd::ClearRenderTargetView, args);
	}

	void WrappedList::ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const UINT Values[4], UINT NumRects, const D3D12_RECT* pRects) {
		real1_->ClearUnorderedAccessViewUint(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
		ClearUavUintArgs args = {ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, {Values[0], Values[1], Values[2], Values[3]}, pRects ? NumRects : 0, pRects};
		Record(Cmd::ClearUnorderedAccessViewUint, args);
	}

	void WrappedList::ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const FLOAT Values[4], UINT NumRects, const D3D12_RECT* pRects) {
		real1_->ClearUnorderedAccessViewFloat(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
		ClearUavFloatArgs args = {ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, {Values[0], Values[1], Values[2], Values[3]}, pRects ? NumRects : 0, pRects};
		Record(Cmd::ClearUnorderedAccessViewFloat, args);
	}

	void WrappedList::DiscardResource(ID3D12Resource* pResource, const D3D12_DISCARD_REGION* pRegion) {
		real1_->DiscardResource(pResource, pRegion);
		DiscardArgs args = {pResource, pRegion};
		Record(Cmd::DiscardResource, args);
	}

	void WrappedList::BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) {
		real1_->BeginQuery(pQueryHeap, Type, Index);
		QueryArgs args = {pQueryHeap, Type, Index};
		Record(Cmd::BeginQuery, args);
	}

	void WrappedList::EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) {
		real1_->EndQuery(pQueryHeap, Type, Index);
		QueryArgs args = {pQueryHeap, Type, Index};
		Record(Cmd::EndQuery, args);
	}

	void WrappedList::ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource* pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) {
		real1_->ResolveQueryData(pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset);
		ResolveQueryArgs args = {pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset};
		Record(Cmd::ResolveQueryData, args);
	}

	void WrappedList::SetPredication(ID3D12Resource* pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) {
		real1_->SetPredication(pBuffer, AlignedBufferOffset, Operation);
		PredicationArgs args = {pBuffer, AlignedBufferOffset, Operation};
		Record(Cmd::SetPredication, args);
	}

	void WrappedList::SetMarker(UINT Metadata, const void* pData, UINT Size) {
		real1_->SetMarker(Metadata, pData, Size);
		MarkerArgs args = {Metadata, pData, pData ? Size : 0};
		Record(Cmd::SetMarker, args);
	}

	void WrappedList::BeginEvent(UINT Metadata, const void* pData, UINT Size) {
		real1_->BeginEvent(Metadata, pData, Size);
		MarkerArgs args = {Metadata, pData, pData ? Size : 0};
		Record(Cmd::BeginEvent, args);
	}

	void WrappedList::EndEvent(void) {
		real1_->EndEvent();
		u8 none = 0;
		Record(Cmd::EndEvent, none);
	}

	void WrappedList::ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, UINT MaxCommandCount, ID3D12Resource* pArgumentBuffer, UINT64 ArgumentBufferOffset, ID3D12Resource* pCountBuffer, UINT64 CountBufferOffset) {
		real1_->ExecuteIndirect(pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset);
		ExecuteIndirectArgs args = {pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset};
		Record(Cmd::ExecuteIndirect, args);
	}

	void WrappedList::AtomicCopyBufferUINT(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT Dependencies, ID3D12Resource* const* ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges) {
		real2_->AtomicCopyBufferUINT(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, Dependencies, ppDependentResources, pDependentSubresourceRanges);
		AtomicCopyArgs args = {pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, Dependencies, ppDependentResources, pDependentSubresourceRanges};
		Record(Cmd::AtomicCopyBufferUINT, args);
	}

	void WrappedList::AtomicCopyBufferUINT64(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT Dependencies, ID3D12Resource* const* ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges) {
		real2_->AtomicCopyBufferUINT64(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, Dependencies, ppDependentResources, pDependentSubresourceRanges);
		AtomicCopyArgs args = {pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, Dependencies, ppDependentResources, pDependentSubresourceRanges};
		Record(Cmd::AtomicCopyBufferUINT64, args);
	}

	void WrappedList::OMSetDepthBounds(FLOAT Min, FLOAT Max) {
		real2_->OMSetDepthBounds(Min, Max);
		DepthBoundsArgs args = {Min, Max};
		Record(Cmd::OMSetDepthBounds, args);
	}

	void WrappedList::SetSamplePositions(UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION* pSamplePositions) {
		real2_->SetSamplePositions(NumSamplesPerPixel, NumPixels, pSamplePositions);
		SamplePositionsArgs args = {NumSamplesPerPixel, pSamplePositions ? NumPixels : 0, pSamplePositions};
		Record(Cmd::SetSamplePositions, args);
	}

	void WrappedList::ResolveSubresourceRegion(ID3D12Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, ID3D12Resource* pSrcResource, UINT SrcSubresource, D3D12_RECT* pSrcRect, DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode) {
		real2_->ResolveSubresourceRegion(pDstResource, DstSubresource, DstX, DstY, pSrcResource, SrcSubresource, pSrcRect, Format, ResolveMode);
		ResolveRegionArgs args = {pDstResource, DstSubresource, DstX, DstY, pSrcResource, SrcSubresource, pSrcRect, Format, ResolveMode};
		Record(Cmd::ResolveSubresourceRegion, args);
	}

	void WrappedList::SetViewInstanceMask(UINT Mask) {
		real2_->SetViewInstanceMask(Mask);
		ViewInstanceMaskArgs args = {Mask};
		Record(Cmd::SetViewInstanceMask, args);
	}

	void WrappedList::WriteBufferImmediate(UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* pParams, const D3D12_WRITEBUFFERIMMEDIATE_MODE* pModes) {
		real3_->WriteBufferImmediate(Count, pParams, pModes);
		WriteImmediateArgs args = {Count, pParams, pModes};
		Record(Cmd::WriteBufferImmediate, args);
	}

	void WrappedList::SetProtectedResourceSession(ID3D12ProtectedResourceSession* pProtectedResourceSession) {
		real4_->SetProtectedResourceSession(pProtectedResourceSession);
		if (pProtectedResourceSession) Unsupported("SetProtectedResourceSession");
	}

	void WrappedList::BeginRenderPass(UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC* pRenderTargets, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags) {
		real5_->BeginRenderPass(NumRenderTargets, pRenderTargets, pDepthStencil, Flags);
		BeginRenderPassArgs args = {pRenderTargets ? NumRenderTargets : 0, pRenderTargets, pDepthStencil, Flags};
		Record(Cmd::BeginRenderPass, args);
	}

	void WrappedList::EndRenderPass(void) {
		real5_->EndRenderPass();
		u8 none = 0;
		Record(Cmd::EndRenderPass, none);
	}

	void WrappedList::InitializeMetaCommand(ID3D12MetaCommand* pMetaCommand, const void* pInitializationParametersData, SIZE_T InitializationParametersDataSizeInBytes) {
		real5_->InitializeMetaCommand(pMetaCommand, pInitializationParametersData, InitializationParametersDataSizeInBytes);
		Unsupported("InitializeMetaCommand");
	}

	void WrappedList::ExecuteMetaCommand(ID3D12MetaCommand* pMetaCommand, const void* pExecutionParametersData, SIZE_T ExecutionParametersDataSizeInBytes) {
		real5_->ExecuteMetaCommand(pMetaCommand, pExecutionParametersData, ExecutionParametersDataSizeInBytes);
		Unsupported("ExecuteMetaCommand");
	}

	void WrappedList::BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc, UINT NumPostbuildInfoDescs, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pPostbuildInfoDescs) {
		real5_->BuildRaytracingAccelerationStructure(pDesc, NumPostbuildInfoDescs, pPostbuildInfoDescs);
		Unsupported("BuildRaytracingAccelerationStructure");
	}

	void WrappedList::EmitRaytracingAccelerationStructurePostbuildInfo(const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pDesc, UINT NumSourceAccelerationStructures, const D3D12_GPU_VIRTUAL_ADDRESS* pSourceAccelerationStructureData) {
		real5_->EmitRaytracingAccelerationStructurePostbuildInfo(pDesc, NumSourceAccelerationStructures, pSourceAccelerationStructureData);
		Unsupported("EmitRaytracingAccelerationStructurePostbuildInfo");
	}

	void WrappedList::CopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData, D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode) {
		real5_->CopyRaytracingAccelerationStructure(DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
		Unsupported("CopyRaytracingAccelerationStructure");
	}

	void WrappedList::SetPipelineState1(ID3D12StateObject* pStateObject) {
		real5_->SetPipelineState1(pStateObject);
		Unsupported("SetPipelineState1");
	}

	void WrappedList::DispatchRays(const D3D12_DISPATCH_RAYS_DESC* pDesc) {
		real5_->DispatchRays(pDesc);
		Unsupported("DispatchRays");
	}

	void WrappedList::RSSetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER* combiners) {
		real6_->RSSetShadingRate(baseShadingRate, combiners);
		ShadingRateArgs args = {};
		args.rate = baseShadingRate;
		if (combiners) {
			args.hasCombiners = 1;
			for (int i = 0; i < D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT; ++i) args.combiners[i] = combiners[i];
		}
		Record(Cmd::RSSetShadingRate, args);
	}

	void WrappedList::RSSetShadingRateImage(ID3D12Resource* shadingRateImage) {
		real6_->RSSetShadingRateImage(shadingRateImage);
		ShadingRateImageArgs args = {shadingRateImage};
		Record(Cmd::RSSetShadingRateImage, args);
	}

	void WrappedList::DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) {
		real7_->DispatchMesh(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
		DispatchArgs args = {ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ};
		Record(Cmd::DispatchMesh, args);
	}

	void WrappedList::Barrier(UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP* pBarrierGroups) {
		real8_->Barrier(NumBarrierGroups, pBarrierGroups);
		EnhancedBarrierArgs args = {NumBarrierGroups, pBarrierGroups};
		Record(Cmd::Barrier, args);
	}

	void WrappedList::OMSetFrontAndBackStencilRef(UINT FrontStencilRef, UINT BackStencilRef) {
		real9_->OMSetFrontAndBackStencilRef(FrontStencilRef, BackStencilRef);
		StencilRefsArgs args = {FrontStencilRef, BackStencilRef};
		Record(Cmd::OMSetFrontAndBackStencilRef, args);
	}

	void WrappedList::RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias) {
		real10_->RSSetDepthBias(DepthBias, DepthBiasClamp, SlopeScaledDepthBias);
		DepthBiasArgs args = {DepthBias, DepthBiasClamp, SlopeScaledDepthBias};
		Record(Cmd::RSSetDepthBias, args);
	}

	void WrappedList::IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) {
		real10_->IASetIndexBufferStripCutValue(IBStripCutValue);
		StripCutArgs args = {IBStripCutValue};
		Record(Cmd::IASetIndexBufferStripCutValue, args);
	}

	void WrappedList::SetProgram(const D3D12_SET_PROGRAM_DESC* pDesc) {
		real11_->SetProgram(pDesc);
		Unsupported("SetProgram");
	}

	void WrappedList::DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC* pDesc) {
		real11_->DispatchGraph(pDesc);
		Unsupported("DispatchGraph");
	}
}
