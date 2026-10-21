// wrappers.h - the proxies a D3D12 program gets back when it hands its device and DXGI factory to the capture library
// (docs/22 "Capture"): each forwards every method (forwarders.h) and records what a capture needs - object creation and
// descriptors on the device, every command of a command list, submissions and fences on a queue, frame boundaries on a
// swap chain. Other objects (resources, heaps, pipeline states, ...) stay the runtime's own.
#pragma once

#include "forwarders.h"
#include "framecapture.h"
#include "tracker.h"

#include <atomic>
#include <mutex>

namespace fb::capture::d3d12
{
	class WrappedDevice;
	class WrappedQueue;

	// QueryInterface with one of these answers the proxy of that kind itself, so the library tells its proxies from the
	// runtime's objects (and from each other) wherever the program hands one back.
	extern const GUID kDeviceProxyIid;
	extern const GUID kQueueProxyIid;
	extern const GUID kListProxyIid;
	extern const GUID kFactoryProxyIid;
	extern const GUID kSwapChainProxyIid;

	// A command list's recorded commands since its last Reset, and what its barriers do to resource states.
	struct ListRecording
	{
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ByteWriter commands;
		u32 count = 0;
		struct Transition
		{
			Object* resource;
			u32 subresource;
			D3D12_RESOURCE_STATES after;
		};
		std::vector<Transition> transitions;
		std::vector<std::string> unsupported;
		bool bundles = false;
	};

	class WrappedList final : public ListForwarder
	{
	public:
		WrappedList(WrappedDevice* device, ID3D12CommandList* real, D3D12_COMMAND_LIST_TYPE type);
		~WrappedList();

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override;

		HRESULT STDMETHODCALLTYPE Close(void) override;
		HRESULT STDMETHODCALLTYPE Reset(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) override;
		void STDMETHODCALLTYPE ClearState(ID3D12PipelineState* pPipelineState) override;
		void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override;
		void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override;
		void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
		void STDMETHODCALLTYPE CopyBufferRegion(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT64 NumBytes) override;
		void STDMETHODCALLTYPE CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION* pSrc, const D3D12_BOX* pSrcBox) override;
		void STDMETHODCALLTYPE CopyResource(ID3D12Resource* pDstResource, ID3D12Resource* pSrcResource) override;
		void STDMETHODCALLTYPE CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate, const D3D12_TILE_REGION_SIZE* pTileRegionSize, ID3D12Resource* pBuffer, UINT64 BufferStartOffsetInBytes, D3D12_TILE_COPY_FLAGS Flags) override;
		void STDMETHODCALLTYPE ResolveSubresource(ID3D12Resource* pDstResource, UINT DstSubresource, ID3D12Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override;
		void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) override;
		void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT* pViewports) override;
		void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D12_RECT* pRects) override;
		void STDMETHODCALLTYPE OMSetBlendFactor(const FLOAT BlendFactor[4]) override;
		void STDMETHODCALLTYPE OMSetStencilRef(UINT StencilRef) override;
		void STDMETHODCALLTYPE SetPipelineState(ID3D12PipelineState* pPipelineState) override;
		void STDMETHODCALLTYPE ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) override;
		void STDMETHODCALLTYPE ExecuteBundle(ID3D12GraphicsCommandList* pCommandList) override;
		void STDMETHODCALLTYPE SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps) override;
		void STDMETHODCALLTYPE SetComputeRootSignature(ID3D12RootSignature* pRootSignature) override;
		void STDMETHODCALLTYPE SetGraphicsRootSignature(ID3D12RootSignature* pRootSignature) override;
		void STDMETHODCALLTYPE SetComputeRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override;
		void STDMETHODCALLTYPE SetGraphicsRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) override;
		void STDMETHODCALLTYPE SetComputeRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override;
		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) override;
		void STDMETHODCALLTYPE SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) override;
		void STDMETHODCALLTYPE SetGraphicsRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) override;
		void STDMETHODCALLTYPE SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) override;
		void STDMETHODCALLTYPE IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) override;
		void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW* pViews) override;
		void STDMETHODCALLTYPE SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews) override;
		void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, BOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) override;
		void STDMETHODCALLTYPE ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT* pRects) override;
		void STDMETHODCALLTYPE ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT* pRects) override;
		void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const UINT Values[4], UINT NumRects, const D3D12_RECT* pRects) override;
		void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const FLOAT Values[4], UINT NumRects, const D3D12_RECT* pRects) override;
		void STDMETHODCALLTYPE DiscardResource(ID3D12Resource* pResource, const D3D12_DISCARD_REGION* pRegion) override;
		void STDMETHODCALLTYPE BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override;
		void STDMETHODCALLTYPE EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) override;
		void STDMETHODCALLTYPE ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource* pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) override;
		void STDMETHODCALLTYPE SetPredication(ID3D12Resource* pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) override;
		void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* pData, UINT Size) override;
		void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* pData, UINT Size) override;
		void STDMETHODCALLTYPE EndEvent(void) override;
		void STDMETHODCALLTYPE ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, UINT MaxCommandCount, ID3D12Resource* pArgumentBuffer, UINT64 ArgumentBufferOffset, ID3D12Resource* pCountBuffer, UINT64 CountBufferOffset) override;
		void STDMETHODCALLTYPE AtomicCopyBufferUINT(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT Dependencies, ID3D12Resource* const* ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges) override;
		void STDMETHODCALLTYPE AtomicCopyBufferUINT64(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT Dependencies, ID3D12Resource* const* ppDependentResources, const D3D12_SUBRESOURCE_RANGE_UINT64* pDependentSubresourceRanges) override;
		void STDMETHODCALLTYPE OMSetDepthBounds(FLOAT Min, FLOAT Max) override;
		void STDMETHODCALLTYPE SetSamplePositions(UINT NumSamplesPerPixel, UINT NumPixels, D3D12_SAMPLE_POSITION* pSamplePositions) override;
		void STDMETHODCALLTYPE ResolveSubresourceRegion(ID3D12Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, ID3D12Resource* pSrcResource, UINT SrcSubresource, D3D12_RECT* pSrcRect, DXGI_FORMAT Format, D3D12_RESOLVE_MODE ResolveMode) override;
		void STDMETHODCALLTYPE SetViewInstanceMask(UINT Mask) override;
		void STDMETHODCALLTYPE WriteBufferImmediate(UINT Count, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* pParams, const D3D12_WRITEBUFFERIMMEDIATE_MODE* pModes) override;
		void STDMETHODCALLTYPE SetProtectedResourceSession(ID3D12ProtectedResourceSession* pProtectedResourceSession) override;
		void STDMETHODCALLTYPE BeginRenderPass(UINT NumRenderTargets, const D3D12_RENDER_PASS_RENDER_TARGET_DESC* pRenderTargets, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil, D3D12_RENDER_PASS_FLAGS Flags) override;
		void STDMETHODCALLTYPE EndRenderPass(void) override;
		void STDMETHODCALLTYPE InitializeMetaCommand(ID3D12MetaCommand* pMetaCommand, const void* pInitializationParametersData, SIZE_T InitializationParametersDataSizeInBytes) override;
		void STDMETHODCALLTYPE ExecuteMetaCommand(ID3D12MetaCommand* pMetaCommand, const void* pExecutionParametersData, SIZE_T ExecutionParametersDataSizeInBytes) override;
		void STDMETHODCALLTYPE BuildRaytracingAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc, UINT NumPostbuildInfoDescs, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pPostbuildInfoDescs) override;
		void STDMETHODCALLTYPE EmitRaytracingAccelerationStructurePostbuildInfo(const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pDesc, UINT NumSourceAccelerationStructures, const D3D12_GPU_VIRTUAL_ADDRESS* pSourceAccelerationStructureData) override;
		void STDMETHODCALLTYPE CopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData, D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode) override;
		void STDMETHODCALLTYPE SetPipelineState1(ID3D12StateObject* pStateObject) override;
		void STDMETHODCALLTYPE DispatchRays(const D3D12_DISPATCH_RAYS_DESC* pDesc) override;
		void STDMETHODCALLTYPE RSSetShadingRate(D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER* combiners) override;
		void STDMETHODCALLTYPE RSSetShadingRateImage(ID3D12Resource* shadingRateImage) override;
		void STDMETHODCALLTYPE DispatchMesh(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
		void STDMETHODCALLTYPE Barrier(UINT32 NumBarrierGroups, const D3D12_BARRIER_GROUP* pBarrierGroups) override;
		void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT FrontStencilRef, UINT BackStencilRef) override;
		void STDMETHODCALLTYPE RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp, FLOAT SlopeScaledDepthBias) override;
		void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) override;
		void STDMETHODCALLTYPE SetProgram(const D3D12_SET_PROGRAM_DESC* pDesc) override;
		void STDMETHODCALLTYPE DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC* pDesc) override;

		ID3D12CommandList* Real() const { return real0_; }
		u64 Id() const { return id_; }
		ListRecording& Recording() { return recording_; }
		// A list made open (CreateCommandList) starts as if Reset with its allocator and initial state.
		void Opened(ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState);

	private:
		template <class A> void Record(Cmd command, A& args);
		void Unsupported(const char* method);

		WrappedDevice* device_;
		u64 id_ = 0;
		ListRecording recording_;
		std::atomic<ULONG> references_{1};
	};

	class WrappedQueue final : public QueueForwarder
	{
	public:
		WrappedQueue(WrappedDevice* device, ID3D12CommandQueue* real);
		~WrappedQueue();

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppvDevice) override;
		void STDMETHODCALLTYPE ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) override;
		HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* pFence, UINT64 Value) override;
		HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* pFence, UINT64 Value) override;
		void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* pData, UINT Size) override;
		void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* pData, UINT Size) override;
		void STDMETHODCALLTYPE EndEvent(void) override;

		ID3D12CommandQueue* Real() const { return real0_; }
		u64 Id() const { return id_; }
		WrappedDevice* Device() const { return device_; }

	private:
		WrappedDevice* device_;
		u64 id_ = 0;
		std::atomic<ULONG> references_{1};
	};

	class WrappedDevice final : public DeviceForwarder
	{
	public:
		explicit WrappedDevice(ID3D12Device* real);
		~WrappedDevice();

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override;

		HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override;
		HRESULT STDMETHODCALLTYPE CreateCommandQueue1(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID CreatorID, REFIID riid, void** ppCommandQueue) override;
		HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;
		HRESULT STDMETHODCALLTYPE CreateCommandList1(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void** ppCommandList) override;
		HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
		HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
		HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
		HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) override;
		HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) override;
		void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CreateSampler2(const D3D12_SAMPLER_DESC2* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
		void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
		void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
		HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateCommittedResource1(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riidResource, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateCommittedResource2(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riidResource, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateCommittedResource3(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC1* pDesc, D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE* pOptimizedClearValue, ID3D12ProtectedResourceSession* pProtectedSession, UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats, REFIID riidResource, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
		HRESULT STDMETHODCALLTYPE CreateHeap1(const D3D12_HEAP_DESC* pDesc, ID3D12ProtectedResourceSession* pProtectedSession, REFIID riid, void** ppvHeap) override;
		HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreatePlacedResource1(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreatePlacedResource2(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC1* pDesc, D3D12_BARRIER_LAYOUT InitialLayout, const D3D12_CLEAR_VALUE* pOptimizedClearValue, UINT32 NumCastableFormats, const DXGI_FORMAT* pCastableFormats, REFIID riid, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
		HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) override;
		HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
		HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppvCommandSignature) override;
		HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) override;
		HRESULT STDMETHODCALLTYPE CreateStateObject(const D3D12_STATE_OBJECT_DESC* pDesc, REFIID riid, void** ppStateObject) override;

		Tracker& Objects() { return tracker_; }
		FrameCapture& Frames() { return frames_; }
		ID3D12Device* Real() const { return real0_; }
		std::vector<WrappedQueue*> Queues();
		void AddQueue(WrappedQueue* queue);
		void RemoveQueue(WrappedQueue* queue);
		// Applies the submitted lists' barriers to the tracked resource states.
		void Submitted(const std::vector<WrappedList*>& lists);
		void Warn(const std::string& text);

		// The proxy of a real device, made or found.
		static WrappedDevice* For(ID3D12Device* real);

	private:
		Object* AddResource(ID3D12Resource* resource, ResourceCreate& record, const D3D12_RESOURCE_DESC1& desc, D3D12_HEAP_TYPE heapType, D3D12_CPU_PAGE_PROPERTY cpuPage, D3D12_RESOURCE_STATES state);
		void AddPipeline(ID3D12PipelineState* pipeline, PipelineCreate& record);
		void View(ViewKind kind, ID3D12Resource* resource, ID3D12Resource* counter, const void* desc, size_t descSize, D3D12_CPU_DESCRIPTOR_HANDLE handle);

		Tracker tracker_;
		FrameCapture frames_;
		std::mutex queuesLock_;
		std::vector<WrappedQueue*> queues_;
		std::atomic<ULONG> references_{1};
	};

	class WrappedSwapChain;

	class WrappedFactory final : public FactoryForwarder
	{
	public:
		explicit WrappedFactory(IDXGIFactory* real);
		~WrappedFactory();

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) override;
		HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) override;
		HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown* pDevice, IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) override;
		HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) override;

	private:
		std::atomic<ULONG> references_{1};
	};

	class WrappedSwapChain final : public SwapChainForwarder
	{
	public:
		WrappedSwapChain(WrappedQueue* queue, IDXGISwapChain* real);
		~WrappedSwapChain();

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
		ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
		ULONG STDMETHODCALLTYPE Release() override;
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;
		HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
		HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue) override;

		u64 Id() const { return id_; }
		WrappedQueue* Queue() const { return queue_; }
		// The resource of the buffer the next Present shows (registered with the tracker).
		ID3D12Resource* CurrentBuffer(u64& id);

	private:
		// Registers the swap chain's buffers as resources and writes their record.
		void RegisterBuffers();

		WrappedQueue* queue_;
		u64 id_ = 0;
		std::vector<u64> buffers_;
		std::atomic<ULONG> references_{1};
	};

	// The C API's entry points (api.cpp): a proxy for a real device or factory, as `riid`.
	HRESULT WrapDevice(IUnknown* device, REFIID riid, void** out);
	HRESULT WrapFactory(IUnknown* factory, REFIID riid, void** out);
	// Every wrapped device, for manual frame bounds.
	std::vector<WrappedDevice*> Devices();
}
