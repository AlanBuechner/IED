// d3d12_records.h - what a D3D12 capture holds (docs/22 "The capture file"): the file-level records (objects, descriptors,
// initial contents, submissions) and the command-list commands, each with the structure of its arguments. The capture
// layer fills these from the calls it hooks; the replayer reads them back and calls the API with them.
#pragma once

#include "ser.h"

#include <d3d12.h>
#include <dxgi1_6.h>

namespace fb::d3d12
{
	enum Record : u32
	{
		kDevice = 100,               // DeviceInfo
		kCreateHeap = 101,           // HeapCreate
		kCreateResource = 102,       // ResourceCreate (committed, placed, reserved, swap chain buffer)
		kCreateDescriptorHeap = 103, // DescriptorHeapCreate
		kCreateRootSignature = 104,  // RootSignatureCreate
		kCreatePipelineState = 105,  // PipelineCreate
		kCreateCommandSignature = 106,
		kCreateQueryHeap = 107,
		kCreateFence = 108,          // FenceCreate (its value at the frame's start)
		kCreateCommandQueue = 109,
		kCreateCommandList = 110,    // ListCreate: a list's id and type, for names
		kSwapChain = 111,            // SwapChainCreate
		kName = 112,                 // ObjectName
		kDescriptor = 113,           // DescriptorWrite: what a heap slot holds
		kCopyDescriptors = 114,      // DescriptorCopy (during the frame)
		kResourceState = 115,        // ResourceState at the frame's start
		kInitialContents = 116,      // InitialContents of one subresource (or a buffer)
		kSubmit = 117,               // Submit: a queue's ExecuteCommandLists with each list's commands
		kMapWrite = 118,             // MapWrite: CPU writes to mapped memory before a submission
		kQueueSignal = 119,          // QueueFence
		kQueueWait = 120,            // QueueFence
		kQueueMarker = 121,          // QueueMarker
		kPresent = 122,              // Present
		kDestroyed = 123,            // ObjectRef: an object released during the frame
		kFenceSignal = 124,          // FenceSignal: ID3D12Fence::Signal from the CPU
	};

	enum class ObjectKind : u32 { Unknown = 0, Device, Heap, Resource, DescriptorHeap, RootSignature, PipelineState, CommandSignature, QueryHeap, Fence, CommandQueue, CommandAllocator, CommandList, SwapChain };
	const char* ObjectKindName(ObjectKind kind);

	struct DeviceInfo
	{
		u64 id;
		std::string adapter;           // DXGI adapter description
		u32 vendorId;
		u32 deviceId;
		u64 luid;
		u64 dedicatedVideoMemory;
		std::string driver;            // driver version "31.0.15.7688"
		D3D_FEATURE_LEVEL featureLevel;
		u32 rtvIncrement;
		u32 dsvIncrement;
		u32 viewIncrement;             // CBV/SRV/UAV
		u32 samplerIncrement;
		u8 debugLayer;
	};

	struct HeapCreate
	{
		u64 id;
		D3D12_HEAP_DESC desc;
	};

	enum class ResourceKind : u32 { Committed = 0, Placed = 1, Reserved = 2, SwapChainBuffer = 3 };

	struct ResourceCreate
	{
		u64 id;
		ResourceKind kind;
		D3D12_HEAP_PROPERTIES heapProperties;
		D3D12_HEAP_FLAGS heapFlags;
		D3D12_RESOURCE_DESC1 desc;
		D3D12_RESOURCE_STATES initialState;
		u32 useLayout;                    // created with an initial layout (CreateCommittedResource3, ...)
		D3D12_BARRIER_LAYOUT initialLayout;
		u8 hasClearValue;
		D3D12_CLEAR_VALUE clearValue;
		u64 heap;                         // placed: its heap
		u64 heapOffset;
		u64 gpuAddress;                   // buffers: the address the program saw
		u32 castableFormatCount;
		const DXGI_FORMAT* castableFormats;
	};

	struct DescriptorHeapCreate
	{
		u64 id;
		D3D12_DESCRIPTOR_HEAP_DESC desc;
		u64 cpuStart;
		u64 gpuStart;
		u32 increment;
	};

	struct RootSignatureCreate
	{
		u64 id;
		u32 nodeMask;
		const void* blob;
		u64 blobSize;
	};

	enum class PipelineKind : u32 { Graphics = 0, Compute = 1, Mesh = 2 };

	// Every way of making a pipeline state (graphics and compute descriptions, streams) as one superset; the versions
	// say which rasterizer and depth-stencil description the program used, so a replay makes the same.
	struct PipelineCreate
	{
		u64 id;
		PipelineKind kind;
		u8 fromStream;
		ID3D12RootSignature* rootSignature;
		D3D12_SHADER_BYTECODE vs, ps, ds, hs, gs, cs, as, ms;
		D3D12_STREAM_OUTPUT_DESC streamOutput;
		D3D12_BLEND_DESC blend;
		u32 sampleMask;
		u32 rasterizerVersion;           // 0, 1, 2
		D3D12_RASTERIZER_DESC2 rasterizer;
		u32 depthStencilVersion;         // 0, 1, 2
		D3D12_DEPTH_STENCIL_DESC2 depthStencil;
		D3D12_INPUT_LAYOUT_DESC inputLayout;
		D3D12_INDEX_BUFFER_STRIP_CUT_VALUE stripCut;
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType;
		D3D12_RT_FORMAT_ARRAY renderTargets;
		DXGI_FORMAT depthStencilFormat;
		DXGI_SAMPLE_DESC sampleDesc;
		u32 nodeMask;
		D3D12_PIPELINE_STATE_FLAGS flags;
		D3D12_VIEW_INSTANCING_DESC viewInstancing;
	};

	struct CommandSignatureCreate
	{
		u64 id;
		D3D12_COMMAND_SIGNATURE_DESC desc;
		ID3D12RootSignature* rootSignature;
	};

	struct QueryHeapCreate
	{
		u64 id;
		D3D12_QUERY_HEAP_DESC desc;
	};

	struct FenceCreate
	{
		u64 id;
		u64 value;                // at the frame's start
		D3D12_FENCE_FLAGS flags;
	};

	struct QueueCreate
	{
		u64 id;
		D3D12_COMMAND_QUEUE_DESC desc;
	};

	struct ListCreate
	{
		u64 id;
		D3D12_COMMAND_LIST_TYPE type;
	};

	struct SwapChainCreate
	{
		u64 id;
		u64 queue;
		u32 width;
		u32 height;
		DXGI_FORMAT format;
		u32 bufferCount;
		const u64* buffers;       // resource ids
	};

	struct ObjectName
	{
		u64 id;
		std::string name;
	};

	struct ObjectRef
	{
		u64 id;
	};

	enum class ViewKind : u32 { None = 0, Cbv = 1, Srv = 2, Uav = 3, Rtv = 4, Dsv = 5, Sampler = 6, Sampler2 = 7 };
	const char* ViewKindName(ViewKind kind);

	// A descriptor heap slot's view, as the program made it.
	struct DescriptorWrite
	{
		u64 heap;
		u32 index;
		ViewKind kind;
		ID3D12Resource* resource;
		ID3D12Resource* counter;
		u8 hasDesc;                 // false: the resource's default view (a null desc)
		union
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
			D3D12_SHADER_RESOURCE_VIEW_DESC srv;
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
			D3D12_RENDER_TARGET_VIEW_DESC rtv;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
			D3D12_SAMPLER_DESC2 sampler;
		};
	};

	struct DescriptorCopy
	{
		u64 dstHeap;
		u32 dstIndex;
		u64 srcHeap;
		u32 srcIndex;
		u32 count;
	};

	struct ResourceState
	{
		u64 id;
		u32 count;                       // 1: every subresource the same
		const D3D12_RESOURCE_STATES* states;
		u32 layoutCount;                 // enhanced barriers' layouts, when the program uses them
		const D3D12_BARRIER_LAYOUT* layouts;
	};

	// One subresource's data, rows packed as GetCopyableFootprints lays them out (RowPitch aligned to 256).
	struct InitialContents
	{
		u64 id;
		u32 subresource;
		D3D12_SUBRESOURCE_FOOTPRINT footprint;
		const void* data;
		u64 size;
	};

	struct ListContents
	{
		u64 list;
		const void* commands;     // the list's command stream (CommandHeader + arguments, repeated)
		u64 size;
		u32 commandCount;
	};

	struct Submit
	{
		u64 queue;
		u32 listCount;
		ListContents* lists;
	};

	struct MapWrite
	{
		u64 resource;
		u64 offset;
		const void* data;
		u64 size;
	};

	struct QueueFence
	{
		u64 queue;
		u64 fence;
		u64 value;
	};

	struct FenceSignal
	{
		u64 fence;
		u64 value;
	};

	enum class MarkerOp : u32 { Begin = 0, End = 1, Set = 2 };

	struct QueueMarker
	{
		u64 queue;
		MarkerOp op;
		u32 metadata;
		const void* data;
		u32 size;
	};

	struct Present
	{
		u64 swapChain;
		u64 backBuffer;
		u32 syncInterval;
		u32 flags;
	};

	// --- Commands inside a command list ---------------------------------------------------------------------------

	struct CommandHeader
	{
		u16 command;
		u16 flags;
		u32 size;     // of the arguments that follow
	};

#define FB_D3D12_COMMANDS(X) \
	X(Close) X(Reset) X(ClearState) X(DrawInstanced) X(DrawIndexedInstanced) X(Dispatch) X(CopyBufferRegion) \
	X(CopyTextureRegion) X(CopyResource) X(CopyTiles) X(ResolveSubresource) X(IASetPrimitiveTopology) X(RSSetViewports) \
	X(RSSetScissorRects) X(OMSetBlendFactor) X(OMSetStencilRef) X(SetPipelineState) X(ResourceBarrier) X(ExecuteBundle) \
	X(SetDescriptorHeaps) X(SetComputeRootSignature) X(SetGraphicsRootSignature) X(SetComputeRootDescriptorTable) \
	X(SetGraphicsRootDescriptorTable) X(SetComputeRoot32BitConstant) X(SetGraphicsRoot32BitConstant) \
	X(SetComputeRoot32BitConstants) X(SetGraphicsRoot32BitConstants) X(SetComputeRootConstantBufferView) \
	X(SetGraphicsRootConstantBufferView) X(SetComputeRootShaderResourceView) X(SetGraphicsRootShaderResourceView) \
	X(SetComputeRootUnorderedAccessView) X(SetGraphicsRootUnorderedAccessView) X(IASetIndexBuffer) X(IASetVertexBuffers) \
	X(SOSetTargets) X(OMSetRenderTargets) X(ClearDepthStencilView) X(ClearRenderTargetView) X(ClearUnorderedAccessViewUint) \
	X(ClearUnorderedAccessViewFloat) X(DiscardResource) X(BeginQuery) X(EndQuery) X(ResolveQueryData) X(SetPredication) \
	X(SetMarker) X(BeginEvent) X(EndEvent) X(ExecuteIndirect) X(AtomicCopyBufferUINT) X(AtomicCopyBufferUINT64) \
	X(OMSetDepthBounds) X(SetSamplePositions) X(ResolveSubresourceRegion) X(SetViewInstanceMask) X(WriteBufferImmediate) \
	X(BeginRenderPass) X(EndRenderPass) X(RSSetShadingRate) X(RSSetShadingRateImage) X(DispatchMesh) X(Barrier) \
	X(OMSetFrontAndBackStencilRef) X(RSSetDepthBias) X(IASetIndexBufferStripCutValue) X(Unsupported)

	enum class Cmd : u16
	{
#define FB_ENUM_ITEM(name) name,
		FB_D3D12_COMMANDS(FB_ENUM_ITEM)
#undef FB_ENUM_ITEM
		Count
	};
	const char* CmdName(Cmd command);

	enum class CmdClass : u32 { State = 0, Draw = 1, Dispatch = 2, Copy = 3, Clear = 4, Resolve = 5, Barrier = 6, Marker = 7, Query = 8, Other = 9 };
	CmdClass ClassOf(Cmd command);
	// Draws, dispatches, copies, clears and resolves: what the events grid shows as actions.
	bool IsAction(Cmd command);

	struct ResetArgs { ID3D12CommandAllocator* allocator; ID3D12PipelineState* pipelineState; };
	struct PipelineArgs { ID3D12PipelineState* pipelineState; };
	struct DrawArgs { UINT vertexCount; UINT instanceCount; UINT startVertex; UINT startInstance; };
	struct DrawIndexedArgs { UINT indexCount; UINT instanceCount; UINT startIndex; INT baseVertex; UINT startInstance; };
	struct DispatchArgs { UINT x; UINT y; UINT z; };
	struct CopyBufferRegionArgs { ID3D12Resource* dst; UINT64 dstOffset; ID3D12Resource* src; UINT64 srcOffset; UINT64 numBytes; };
	struct CopyTextureRegionArgs { D3D12_TEXTURE_COPY_LOCATION dst; UINT dstX; UINT dstY; UINT dstZ; D3D12_TEXTURE_COPY_LOCATION src; const D3D12_BOX* srcBox; };
	struct CopyResourceArgs { ID3D12Resource* dst; ID3D12Resource* src; };
	struct CopyTilesArgs { ID3D12Resource* tiled; D3D12_TILED_RESOURCE_COORDINATE start; D3D12_TILE_REGION_SIZE size; ID3D12Resource* buffer; UINT64 bufferOffset; D3D12_TILE_COPY_FLAGS flags; };
	struct ResolveSubresourceArgs { ID3D12Resource* dst; UINT dstSubresource; ID3D12Resource* src; UINT srcSubresource; DXGI_FORMAT format; };
	struct TopologyArgs { D3D12_PRIMITIVE_TOPOLOGY topology; };
	struct ViewportsArgs { UINT count; const D3D12_VIEWPORT* viewports; };
	struct ScissorsArgs { UINT count; const D3D12_RECT* rects; };
	struct BlendFactorArgs { FLOAT factor[4]; };
	struct StencilRefArgs { UINT ref; };
	struct BarrierArgs { UINT count; const D3D12_RESOURCE_BARRIER* barriers; };
	struct BundleArgs { ID3D12GraphicsCommandList* bundle; };
	struct DescriptorHeapsArgs { UINT count; ID3D12DescriptorHeap* const* heaps; };
	struct RootSignatureArgs { ID3D12RootSignature* rootSignature; };
	struct RootTableArgs { UINT index; D3D12_GPU_DESCRIPTOR_HANDLE base; };
	struct RootConstantArgs { UINT index; UINT value; UINT offset; };
	struct RootConstantsArgs { UINT index; UINT count; const UINT* values; UINT offset; };
	struct RootViewArgs { UINT index; D3D12_GPU_VIRTUAL_ADDRESS address; };
	struct IndexBufferArgs { const D3D12_INDEX_BUFFER_VIEW* view; };
	struct VertexBuffersArgs { UINT startSlot; UINT count; const D3D12_VERTEX_BUFFER_VIEW* views; };
	struct SOTargetsArgs { UINT startSlot; UINT count; const D3D12_STREAM_OUTPUT_BUFFER_VIEW* views; };
	// Single-handle ranges are expanded when recorded, so `targets` always holds one handle per target.
	struct RenderTargetsArgs { UINT count; const D3D12_CPU_DESCRIPTOR_HANDLE* targets; const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencil; };
	struct ClearDepthStencilArgs { D3D12_CPU_DESCRIPTOR_HANDLE view; D3D12_CLEAR_FLAGS flags; FLOAT depth; UINT8 stencil; UINT rectCount; const D3D12_RECT* rects; };
	struct ClearRenderTargetArgs { D3D12_CPU_DESCRIPTOR_HANDLE view; FLOAT color[4]; UINT rectCount; const D3D12_RECT* rects; };
	struct ClearUavUintArgs { D3D12_GPU_DESCRIPTOR_HANDLE gpuView; D3D12_CPU_DESCRIPTOR_HANDLE cpuView; ID3D12Resource* resource; UINT values[4]; UINT rectCount; const D3D12_RECT* rects; };
	struct ClearUavFloatArgs { D3D12_GPU_DESCRIPTOR_HANDLE gpuView; D3D12_CPU_DESCRIPTOR_HANDLE cpuView; ID3D12Resource* resource; FLOAT values[4]; UINT rectCount; const D3D12_RECT* rects; };
	struct DiscardArgs { ID3D12Resource* resource; const D3D12_DISCARD_REGION* region; };
	struct QueryArgs { ID3D12QueryHeap* heap; D3D12_QUERY_TYPE type; UINT index; };
	struct ResolveQueryArgs { ID3D12QueryHeap* heap; D3D12_QUERY_TYPE type; UINT start; UINT count; ID3D12Resource* destination; UINT64 offset; };
	struct PredicationArgs { ID3D12Resource* buffer; UINT64 offset; D3D12_PREDICATION_OP op; };
	struct MarkerArgs { UINT metadata; const void* data; UINT size; };
	struct ExecuteIndirectArgs { ID3D12CommandSignature* signature; UINT maxCount; ID3D12Resource* arguments; UINT64 argumentsOffset; ID3D12Resource* countBuffer; UINT64 countOffset; };
	struct AtomicCopyArgs { ID3D12Resource* dst; UINT64 dstOffset; ID3D12Resource* src; UINT64 srcOffset; UINT dependencies; ID3D12Resource* const* dependentResources; const D3D12_SUBRESOURCE_RANGE_UINT64* dependentRanges; };
	struct DepthBoundsArgs { FLOAT min; FLOAT max; };
	struct SamplePositionsArgs { UINT samplesPerPixel; UINT pixels; D3D12_SAMPLE_POSITION* positions; };
	struct ResolveRegionArgs { ID3D12Resource* dst; UINT dstSubresource; UINT dstX; UINT dstY; ID3D12Resource* src; UINT srcSubresource; D3D12_RECT* srcRect; DXGI_FORMAT format; D3D12_RESOLVE_MODE mode; };
	struct ViewInstanceMaskArgs { UINT mask; };
	struct WriteImmediateArgs { UINT count; const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* params; const D3D12_WRITEBUFFERIMMEDIATE_MODE* modes; };
	struct BeginRenderPassArgs { UINT count; const D3D12_RENDER_PASS_RENDER_TARGET_DESC* targets; const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depthStencil; D3D12_RENDER_PASS_FLAGS flags; };
	struct ShadingRateArgs { D3D12_SHADING_RATE rate; D3D12_SHADING_RATE_COMBINER combiners[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT]; u8 hasCombiners; };
	struct ShadingRateImageArgs { ID3D12Resource* image; };
	struct DispatchMeshArgs { UINT x; UINT y; UINT z; };
	struct EnhancedBarrierArgs { UINT32 count; const D3D12_BARRIER_GROUP* groups; };
	struct StencilRefsArgs { UINT front; UINT back; };
	struct DepthBiasArgs { FLOAT bias; FLOAT clamp; FLOAT slopeScaled; };
	struct StripCutArgs { D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value; };
	struct UnsupportedArgs { const char* name; };

	// --- Serialization ----------------------------------------------------------------------------------------------

	void Serialize(Ser& s, DeviceInfo& v);
	void Serialize(Ser& s, HeapCreate& v);
	void Serialize(Ser& s, ResourceCreate& v);
	void Serialize(Ser& s, DescriptorHeapCreate& v);
	void Serialize(Ser& s, RootSignatureCreate& v);
	void Serialize(Ser& s, PipelineCreate& v);
	void Serialize(Ser& s, CommandSignatureCreate& v);
	void Serialize(Ser& s, QueryHeapCreate& v);
	void Serialize(Ser& s, FenceCreate& v);
	void Serialize(Ser& s, QueueCreate& v);
	void Serialize(Ser& s, ListCreate& v);
	void Serialize(Ser& s, SwapChainCreate& v);
	void Serialize(Ser& s, ObjectName& v);
	void Serialize(Ser& s, ObjectRef& v);
	void Serialize(Ser& s, DescriptorWrite& v);
	void Serialize(Ser& s, DescriptorCopy& v);
	void Serialize(Ser& s, ResourceState& v);
	void Serialize(Ser& s, InitialContents& v);
	void Serialize(Ser& s, ListContents& v);
	void Serialize(Ser& s, Submit& v);
	void Serialize(Ser& s, MapWrite& v);
	void Serialize(Ser& s, QueueFence& v);
	void Serialize(Ser& s, FenceSignal& v);
	void Serialize(Ser& s, QueueMarker& v);
	void Serialize(Ser& s, Present& v);

	// The arguments of command `command` (their structure is the command's *Args); the describe pass names them.
	// `args` points at the command's arguments structure.
	void SerializeCommand(Ser& s, Cmd command, void* args);
	// The size of a command's arguments structure.
	size_t CommandArgsSize(Cmd command);

	// Writes a command (header and arguments) to a list's stream.
	template <class A> void WriteCommand(ByteWriter& out, SerWriteMap* map, Cmd command, A& args) {
		const size_t at = out.Size();
		CommandHeader header = {(u16)command, 0, 0};
		out.Put(header);
		Ser s(out, map);
		SerializeCommand(s, command, &args);
		out.PatchU32(at + 4, (u32)(out.Size() - at - sizeof(CommandHeader)));
	}

	// A marker's text from BeginEvent / SetMarker data: PIX's unicode (0) and ANSI (1) forms and the PIX3 blob (2).
	std::string MarkerText(u32 metadata, const void* data, u32 size);
}

// The API's own structures, serialized field by field (d3d12_ser.cpp).
void Serialize(fb::Ser& s, D3D12_HEAP_PROPERTIES& v);
void Serialize(fb::Ser& s, D3D12_HEAP_DESC& v);
void Serialize(fb::Ser& s, DXGI_SAMPLE_DESC& v);
void Serialize(fb::Ser& s, D3D12_RESOURCE_DESC1& v);
void Serialize(fb::Ser& s, D3D12_CLEAR_VALUE& v);
void Serialize(fb::Ser& s, D3D12_DESCRIPTOR_HEAP_DESC& v);
void Serialize(fb::Ser& s, D3D12_SHADER_BYTECODE& v);
void Serialize(fb::Ser& s, D3D12_SO_DECLARATION_ENTRY& v);
void Serialize(fb::Ser& s, D3D12_STREAM_OUTPUT_DESC& v);
void Serialize(fb::Ser& s, D3D12_RENDER_TARGET_BLEND_DESC& v);
void Serialize(fb::Ser& s, D3D12_BLEND_DESC& v);
void Serialize(fb::Ser& s, D3D12_RASTERIZER_DESC2& v);
void Serialize(fb::Ser& s, D3D12_DEPTH_STENCILOP_DESC1& v);
void Serialize(fb::Ser& s, D3D12_DEPTH_STENCIL_DESC2& v);
void Serialize(fb::Ser& s, D3D12_INPUT_ELEMENT_DESC& v);
void Serialize(fb::Ser& s, D3D12_INPUT_LAYOUT_DESC& v);
void Serialize(fb::Ser& s, D3D12_RT_FORMAT_ARRAY& v);
void Serialize(fb::Ser& s, D3D12_VIEW_INSTANCE_LOCATION& v);
void Serialize(fb::Ser& s, D3D12_VIEW_INSTANCING_DESC& v);
void Serialize(fb::Ser& s, D3D12_INDIRECT_ARGUMENT_DESC& v);
void Serialize(fb::Ser& s, D3D12_COMMAND_SIGNATURE_DESC& v);
void Serialize(fb::Ser& s, D3D12_QUERY_HEAP_DESC& v);
void Serialize(fb::Ser& s, D3D12_COMMAND_QUEUE_DESC& v);
void Serialize(fb::Ser& s, D3D12_CONSTANT_BUFFER_VIEW_DESC& v);
void Serialize(fb::Ser& s, D3D12_SHADER_RESOURCE_VIEW_DESC& v);
void Serialize(fb::Ser& s, D3D12_UNORDERED_ACCESS_VIEW_DESC& v);
void Serialize(fb::Ser& s, D3D12_RENDER_TARGET_VIEW_DESC& v);
void Serialize(fb::Ser& s, D3D12_DEPTH_STENCIL_VIEW_DESC& v);
void Serialize(fb::Ser& s, D3D12_SAMPLER_DESC2& v);
void Serialize(fb::Ser& s, D3D12_SUBRESOURCE_FOOTPRINT& v);
void Serialize(fb::Ser& s, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& v);
void Serialize(fb::Ser& s, D3D12_TEXTURE_COPY_LOCATION& v);
void Serialize(fb::Ser& s, D3D12_BOX& v);
void Serialize(fb::Ser& s, D3D12_TILED_RESOURCE_COORDINATE& v);
void Serialize(fb::Ser& s, D3D12_TILE_REGION_SIZE& v);
void Serialize(fb::Ser& s, D3D12_VIEWPORT& v);
void Serialize(fb::Ser& s, D3D12_RECT& v);
void Serialize(fb::Ser& s, D3D12_RESOURCE_BARRIER& v);
void Serialize(fb::Ser& s, D3D12_INDEX_BUFFER_VIEW& v);
void Serialize(fb::Ser& s, D3D12_VERTEX_BUFFER_VIEW& v);
void Serialize(fb::Ser& s, D3D12_STREAM_OUTPUT_BUFFER_VIEW& v);
void Serialize(fb::Ser& s, D3D12_DISCARD_REGION& v);
void Serialize(fb::Ser& s, D3D12_SUBRESOURCE_RANGE_UINT64& v);
void Serialize(fb::Ser& s, D3D12_SAMPLE_POSITION& v);
void Serialize(fb::Ser& s, D3D12_WRITEBUFFERIMMEDIATE_PARAMETER& v);
void Serialize(fb::Ser& s, D3D12_RENDER_PASS_BEGINNING_ACCESS& v);
void Serialize(fb::Ser& s, D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS& v);
void Serialize(fb::Ser& s, D3D12_RENDER_PASS_ENDING_ACCESS& v);
void Serialize(fb::Ser& s, D3D12_RENDER_PASS_RENDER_TARGET_DESC& v);
void Serialize(fb::Ser& s, D3D12_RENDER_PASS_DEPTH_STENCIL_DESC& v);
void Serialize(fb::Ser& s, D3D12_GLOBAL_BARRIER& v);
void Serialize(fb::Ser& s, D3D12_TEXTURE_BARRIER& v);
void Serialize(fb::Ser& s, D3D12_BUFFER_BARRIER& v);
void Serialize(fb::Ser& s, D3D12_BARRIER_GROUP& v);
void Serialize(fb::Ser& s, D3D12_BARRIER_SUBRESOURCE_RANGE& v);
void Serialize(fb::Ser& s, D3D12_SUBRESOURCE_FOOTPRINT& v);

// Descriptor handles read as "heap[index]" in descriptions.
void SerField(fb::Ser& s, const char* name, D3D12_CPU_DESCRIPTOR_HANDLE& v);
void SerField(fb::Ser& s, const char* name, D3D12_GPU_DESCRIPTOR_HANDLE& v);
