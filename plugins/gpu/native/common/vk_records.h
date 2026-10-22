// vk_records.h - what a Vulkan capture holds (docs/22 "The capture file"): the file-level records (the device, objects in
// creation order, descriptor contents, image layouts, initial contents, submissions) and the command-buffer commands,
// each with the structure of its arguments. The Vulkan layer fills them from the calls it intercepts; the replayer reads
// them back and calls the API with them. Vulkan's own structures serialize through vk_gen.h, generated from vk.xml.
#pragma once

#include "formats.h"
#include "vk_gen.h"

namespace fb::vk
{
	enum Record : u32
	{
		kDevice = 1000,              // DeviceInfo
		kQueue = 1001,               // QueueCreate
		kMemory = 1002,              // MemoryCreate
		kBuffer = 1003,              // BufferCreate
		kImage = 1004,               // ImageCreate (a swap chain's images too)
		kImageView = 1005,           // Create<VkImageViewCreateInfo>
		kBufferView = 1006,          // Create<VkBufferViewCreateInfo>
		kSampler = 1007,             // Create<VkSamplerCreateInfo>
		kShaderModule = 1008,        // Create<VkShaderModuleCreateInfo>
		kDescriptorSetLayout = 1009, // Create<VkDescriptorSetLayoutCreateInfo>
		kPipelineLayout = 1010,      // Create<VkPipelineLayoutCreateInfo>
		kDescriptorPool = 1011,      // Create<VkDescriptorPoolCreateInfo>
		kDescriptorSet = 1012,       // DescriptorSetCreate
		kRenderPass = 1013,          // RenderPassCreate
		kFramebuffer = 1014,         // Create<VkFramebufferCreateInfo>
		kGraphicsPipeline = 1015,    // Create<VkGraphicsPipelineCreateInfo>
		kComputePipeline = 1016,     // Create<VkComputePipelineCreateInfo>
		kCommandPool = 1017,         // Create<VkCommandPoolCreateInfo>
		kCommandBuffer = 1018,       // CommandBufferCreate
		kFence = 1019,               // Create<VkFenceCreateInfo>
		kSemaphore = 1020,           // Create<VkSemaphoreCreateInfo>
		kEvent = 1021,               // Create<VkEventCreateInfo>
		kQueryPool = 1022,           // Create<VkQueryPoolCreateInfo>
		kSwapchain = 1023,           // SwapchainCreate
		kName = 1024,                // ObjectName
		kDescriptorUpdate = 1025,    // DescriptorUpdate: the sets' contents at the frame's start, updates during it
		kImageLayout = 1026,         // ImageLayout at the frame's start
		kMemoryContents = 1027,      // MemoryContents: host-visible memory
		kBufferContents = 1028,      // BufferContents: a buffer in device memory
		kImageContents = 1029,       // ImageContents: one subresource of an image in device memory
		kSubmit = 1030,              // Submit
		kMapWrite = 1031,            // MapWrite: CPU writes to mapped memory before a submission
		kPresent = 1032,             // Present
		kQueueLabel = 1033,          // QueueLabel
		kDestroyed = 1034,           // ObjectRef: an object destroyed during the frame
	};

	enum class ObjectKind : u32
	{
		Unknown = 0, Device, Queue, Memory, Buffer, Image, ImageView, BufferView, Sampler, ShaderModule, DescriptorSetLayout,
		PipelineLayout, DescriptorPool, DescriptorSet, RenderPass, Framebuffer, Pipeline, CommandPool, CommandBuffer, Fence,
		Semaphore, Event, QueryPool, Swapchain
	};
	const char* ObjectKindName(ObjectKind kind);
	ObjectKind KindOfRecord(u32 record);

	struct MemoryTypeInfo
	{
		u32 propertyFlags;   // VkMemoryPropertyFlags
		u32 heapIndex;
	};

	// The device and where it ran: the physical device's identity and memory types (a replay on the same kind of GPU
	// keeps the program's memory type indices), and the device as the program made it.
	struct DeviceInfo
	{
		u64 id;
		u32 instanceApiVersion;
		std::string application;
		std::string engine;
		u32 apiVersion;
		u32 driverVersion;
		u32 vendorId;
		u32 deviceId;
		u32 deviceType;            // VkPhysicalDeviceType
		std::string deviceName;
		std::string driverName;    // VkPhysicalDeviceDriverProperties, when the driver has them
		std::string driverInfo;
		u8 pipelineCacheUuid[16];
		u32 memoryTypeCount;
		const MemoryTypeInfo* memoryTypes;
		u32 instanceExtensionCount;
		const char* const* instanceExtensions;
		VkDeviceCreateInfo create;
	};

	struct QueueCreate
	{
		u64 id;
		u32 family;
		u32 index;
		u32 familyFlags;           // VkQueueFlags of its family
	};

	struct MemoryCreate
	{
		u64 id;
		VkMemoryAllocateInfo info;
		u32 propertyFlags;         // of its memory type
	};

	// Buffers and images as the program made them (with the transfer usages the layer adds, so they can be read and
	// restored), and where they are bound.
	struct BufferCreate
	{
		u64 id;
		VkBufferCreateInfo info;
		u64 memory;
		u64 memoryOffset;
		u64 size;                  // its memory requirements' size
	};

	struct ImageCreate
	{
		u64 id;
		VkImageCreateInfo info;
		u64 memory;
		u64 memoryOffset;
		u64 size;
		u64 swapchain;             // a swap chain's image: no memory of its own
		u32 swapchainIndex;
	};

	template <class Info> struct Create
	{
		u64 id;
		Info info;
	};

	struct DescriptorSetCreate
	{
		u64 id;
		u64 pool;
		u64 layout;
		u32 variableCount;         // VkDescriptorSetVariableDescriptorCountAllocateInfo, 0 when none
	};

	struct RenderPassCreate
	{
		u64 id;
		u32 version;               // 1: VkRenderPassCreateInfo, 2: VkRenderPassCreateInfo2
		VkRenderPassCreateInfo info;
		VkRenderPassCreateInfo2 info2;
	};

	struct CommandBufferCreate
	{
		u64 id;
		u64 pool;
		VkCommandBufferLevel level;
	};

	struct SwapchainCreate
	{
		u64 id;
		VkSwapchainCreateInfoKHR info;
		u32 imageCount;
		const u64* images;
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

	struct DescriptorUpdate
	{
		u32 writeCount;
		const VkWriteDescriptorSet* writes;
		u32 copyCount;
		const VkCopyDescriptorSet* copies;
	};

	// An image's layout per subresource (layer-major: layer * mipLevels + mip) at the frame's start.
	struct ImageLayout
	{
		u64 image;
		u32 count;
		const VkImageLayout* layouts;
	};

	struct MemoryContents
	{
		u64 memory;
		u64 offset;
		const void* data;
		u64 size;
	};

	struct BufferContents
	{
		u64 buffer;
		const void* data;
		u64 size;
	};

	// One subresource, tightly packed as vkCmdCopyImageToBuffer writes it with bufferRowLength 0.
	struct ImageContents
	{
		u64 image;
		u32 aspect;                // VkImageAspectFlagBits
		u32 mip;
		u32 layer;
		VkExtent3D extent;
		const void* data;
		u64 size;
	};

	struct CommandBufferContents
	{
		u64 commandBuffer;
		const void* commands;      // CommandHeader + arguments, repeated
		u64 size;
		u32 commandCount;
	};

	struct SubmitBatch
	{
		u32 waitCount;
		const u64* waitSemaphores;
		u32 commandBufferCount;
		CommandBufferContents* commandBuffers;
		u32 signalCount;
		const u64* signalSemaphores;
	};

	struct Submit
	{
		u64 queue;
		u32 batchCount;
		SubmitBatch* batches;
		u64 fence;
	};

	struct MapWrite
	{
		u64 memory;
		u64 offset;
		const void* data;
		u64 size;
	};

	struct Present
	{
		u64 queue;
		u64 swapchain;             // 0: a frame ended by Gpu::Capture::EndFrame
		u32 imageIndex;
		u64 image;
	};

	enum class LabelOp : u32 { Begin = 0, End = 1, Insert = 2 };

	struct QueueLabel
	{
		u64 queue;
		LabelOp op;
		std::string name;
		float color[4];
	};

	// --- Commands inside a command buffer ----------------------------------------------------------------------------

	struct CommandHeader
	{
		u16 command;
		u16 flags;
		u32 size;     // of the arguments that follow
	};

#define FB_VK_COMMANDS(X) \
	X(BindPipeline) X(SetViewport) X(SetScissor) X(SetLineWidth) X(SetDepthBias) X(SetBlendConstants) X(SetDepthBounds) \
	X(SetStencilCompareMask) X(SetStencilWriteMask) X(SetStencilReference) X(BindDescriptorSets) X(BindIndexBuffer) \
	X(BindVertexBuffers) X(Draw) X(DrawIndexed) X(DrawIndirect) X(DrawIndexedIndirect) X(DrawIndirectCount) \
	X(DrawIndexedIndirectCount) X(Dispatch) X(DispatchBase) X(DispatchIndirect) X(CopyBuffer) X(CopyImage) X(BlitImage) \
	X(CopyBufferToImage) X(CopyImageToBuffer) X(UpdateBuffer) X(FillBuffer) X(ClearColorImage) X(ClearDepthStencilImage) \
	X(ClearAttachments) X(ResolveImage) X(CopyBuffer2) X(CopyImage2) X(BlitImage2) X(CopyBufferToImage2) \
	X(CopyImageToBuffer2) X(ResolveImage2) X(PipelineBarrier) X(PipelineBarrier2) X(PushConstants) X(BeginRenderPass) \
	X(NextSubpass) X(EndRenderPass) X(BeginRendering) X(EndRendering) X(BeginLabel) X(EndLabel) X(InsertLabel) \
	X(ResetQueryPool) X(BeginQuery) X(EndQuery) X(WriteTimestamp) X(CopyQueryPoolResults) X(SetDynamic) \
	X(ExecuteCommands) X(Unsupported)

	enum class Cmd : u16
	{
#define FB_ENUM_ITEM(name) name,
		FB_VK_COMMANDS(FB_ENUM_ITEM)
#undef FB_ENUM_ITEM
		Count
	};
	const char* CmdName(Cmd command);

	enum class CmdClass : u32 { State = 0, Draw = 1, Dispatch = 2, Copy = 3, Clear = 4, Resolve = 5, Barrier = 6, Marker = 7, Query = 8, Other = 9 };
	CmdClass ClassOf(Cmd command);
	bool IsAction(Cmd command);

	struct BindPipelineArgs { VkPipelineBindPoint bindPoint; VkPipeline pipeline; };
	struct ViewportArgs { u32 first; u32 count; const VkViewport* viewports; };
	struct ScissorArgs { u32 first; u32 count; const VkRect2D* scissors; };
	struct FloatArgs { float value; };
	struct DepthBiasArgs { float constant; float clamp; float slope; };
	struct BlendConstantsArgs { float constants[4]; };
	struct DepthBoundsArgs { float min; float max; };
	struct StencilArgs { u32 faces; u32 value; };
	struct BindDescriptorSetsArgs { VkPipelineBindPoint bindPoint; VkPipelineLayout layout; u32 firstSet; u32 setCount; const VkDescriptorSet* sets; u32 dynamicOffsetCount; const u32* dynamicOffsets; };
	struct BindIndexBufferArgs { VkBuffer buffer; VkDeviceSize offset; VkDeviceSize size; VkIndexType indexType; };
	// vkCmdBindVertexBuffers and ...2: sizes and strides absent (null) for the first.
	struct BindVertexBuffersArgs { u32 first; u32 count; const VkBuffer* buffers; const VkDeviceSize* offsets; const VkDeviceSize* sizes; const VkDeviceSize* strides; };
	struct DrawArgs { u32 vertexCount; u32 instanceCount; u32 firstVertex; u32 firstInstance; };
	struct DrawIndexedArgs { u32 indexCount; u32 instanceCount; u32 firstIndex; i32 vertexOffset; u32 firstInstance; };
	struct DrawIndirectArgs { VkBuffer buffer; VkDeviceSize offset; u32 drawCount; u32 stride; };
	struct DrawIndirectCountArgs { VkBuffer buffer; VkDeviceSize offset; VkBuffer countBuffer; VkDeviceSize countOffset; u32 maxDrawCount; u32 stride; };
	struct DispatchArgs { u32 x; u32 y; u32 z; };
	struct DispatchBaseArgs { u32 baseX; u32 baseY; u32 baseZ; u32 x; u32 y; u32 z; };
	struct DispatchIndirectArgs { VkBuffer buffer; VkDeviceSize offset; };
	struct CopyBufferArgs { VkBuffer src; VkBuffer dst; u32 count; const VkBufferCopy* regions; };
	struct CopyImageArgs { VkImage src; VkImageLayout srcLayout; VkImage dst; VkImageLayout dstLayout; u32 count; const VkImageCopy* regions; };
	struct BlitImageArgs { VkImage src; VkImageLayout srcLayout; VkImage dst; VkImageLayout dstLayout; u32 count; const VkImageBlit* regions; VkFilter filter; };
	struct CopyBufferToImageArgs { VkBuffer src; VkImage dst; VkImageLayout dstLayout; u32 count; const VkBufferImageCopy* regions; };
	struct CopyImageToBufferArgs { VkImage src; VkImageLayout srcLayout; VkBuffer dst; u32 count; const VkBufferImageCopy* regions; };
	struct UpdateBufferArgs { VkBuffer dst; VkDeviceSize offset; VkDeviceSize size; const void* data; };
	struct FillBufferArgs { VkBuffer dst; VkDeviceSize offset; VkDeviceSize size; u32 data; };
	struct ClearColorImageArgs { VkImage image; VkImageLayout layout; VkClearColorValue color; u32 count; const VkImageSubresourceRange* ranges; };
	struct ClearDepthStencilImageArgs { VkImage image; VkImageLayout layout; VkClearDepthStencilValue value; u32 count; const VkImageSubresourceRange* ranges; };
	struct ClearAttachmentsArgs { u32 attachmentCount; const VkClearAttachment* attachments; u32 rectCount; const VkClearRect* rects; };
	struct ResolveImageArgs { VkImage src; VkImageLayout srcLayout; VkImage dst; VkImageLayout dstLayout; u32 count; const VkImageResolve* regions; };
	struct CopyBuffer2Args { const VkCopyBufferInfo2* info; };
	struct CopyImage2Args { const VkCopyImageInfo2* info; };
	struct BlitImage2Args { const VkBlitImageInfo2* info; };
	struct CopyBufferToImage2Args { const VkCopyBufferToImageInfo2* info; };
	struct CopyImageToBuffer2Args { const VkCopyImageToBufferInfo2* info; };
	struct ResolveImage2Args { const VkResolveImageInfo2* info; };
	struct PipelineBarrierArgs { u32 srcStages; u32 dstStages; u32 dependencyFlags; u32 memoryCount; const VkMemoryBarrier* memory; u32 bufferCount; const VkBufferMemoryBarrier* buffers; u32 imageCount; const VkImageMemoryBarrier* images; };
	struct PipelineBarrier2Args { const VkDependencyInfo* info; };
	struct PushConstantsArgs { VkPipelineLayout layout; u32 stages; u32 offset; u32 size; const void* values; };
	// vkCmdBeginRenderPass and ...2 (contents from VkSubpassBeginInfo).
	struct BeginRenderPassArgs { const VkRenderPassBeginInfo* begin; VkSubpassContents contents; };
	struct NextSubpassArgs { VkSubpassContents contents; };
	struct EmptyArgs { u32 unused; };
	struct BeginRenderingArgs { const VkRenderingInfo* info; };
	struct LabelArgs { const char* name; float color[4]; };
	struct ResetQueryPoolArgs { VkQueryPool pool; u32 first; u32 count; };
	struct BeginQueryArgs { VkQueryPool pool; u32 query; u32 flags; };
	struct EndQueryArgs { VkQueryPool pool; u32 query; };
	struct WriteTimestampArgs { u64 stage; VkQueryPool pool; u32 query; };
	struct CopyQueryPoolResultsArgs { VkQueryPool pool; u32 first; u32 count; VkBuffer dst; VkDeviceSize offset; VkDeviceSize stride; u32 flags; };
	// The dynamic states of Vulkan 1.3 and VK_EXT_extended_dynamic_state(2): which state, and its value.
	struct SetDynamicArgs { VkDynamicState state; u32 values[6]; };
	// A secondary command buffer's commands are inlined after this, up to the matching end (flags 1).
	struct ExecuteCommandsArgs { u64 commandBuffer; };
	struct UnsupportedArgs { const char* name; };

	// --- Serialization ----------------------------------------------------------------------------------------------

	void Serialize(Ser& s, MemoryTypeInfo& v);
	void Serialize(Ser& s, DeviceInfo& v);
	void Serialize(Ser& s, QueueCreate& v);
	void Serialize(Ser& s, MemoryCreate& v);
	void Serialize(Ser& s, BufferCreate& v);
	void Serialize(Ser& s, ImageCreate& v);
	template <class Info> void Serialize(Ser& s, Create<Info>& v) {
		s.Field("id", v.id);
		s.Field("info", v.info);
	}
	void Serialize(Ser& s, DescriptorSetCreate& v);
	void Serialize(Ser& s, RenderPassCreate& v);
	void Serialize(Ser& s, CommandBufferCreate& v);
	void Serialize(Ser& s, SwapchainCreate& v);
	void Serialize(Ser& s, ObjectName& v);
	void Serialize(Ser& s, ObjectRef& v);
	void Serialize(Ser& s, DescriptorUpdate& v);
	void Serialize(Ser& s, ImageLayout& v);
	void Serialize(Ser& s, MemoryContents& v);
	void Serialize(Ser& s, BufferContents& v);
	void Serialize(Ser& s, ImageContents& v);
	void Serialize(Ser& s, CommandBufferContents& v);
	void Serialize(Ser& s, SubmitBatch& v);
	void Serialize(Ser& s, Submit& v);
	void Serialize(Ser& s, MapWrite& v);
	void Serialize(Ser& s, Present& v);
	void Serialize(Ser& s, QueueLabel& v);

	// The arguments of `command` (their structure is the command's *Args); `args` points at it.
	void SerializeCommand(Ser& s, Cmd command, void* args);
	size_t CommandArgsSize(Cmd command);

	template <class A> void WriteCommand(ByteWriter& out, SerWriteMap* map, Cmd command, A& args, u16 flags = 0) {
		const size_t at = out.Size();
		CommandHeader header = {(u16)command, flags, 0};
		out.Put(header);
		Ser s(out, map);
		SerializeCommand(s, command, &args);
		out.PatchU32(at + 4, (u32)(out.Size() - at - sizeof(CommandHeader)));
	}

	// The texel block of a format for copies: bytes per block and the block's size in texels (1x1 for uncompressed).
	struct FormatBlock
	{
		u32 bytes;
		u32 width;
		u32 height;
	};
	// For one aspect of `format` (depth and stencil copy apart); bytes 0 for a format Flashbang cannot copy.
	FormatBlock BlockOf(VkFormat format, VkImageAspectFlagBits aspect);
	// The aspects an image of `format` has (color, or depth and/or stencil).
	u32 AspectsOf(VkFormat format);
	// The DXGI format with the memory layout of `format`'s `aspect` as a copy to a buffer writes it (UNKNOWN when there is
	// none), and its decoder's description (null then).
	DXGI_FORMAT DxgiOf(VkFormat format, VkImageAspectFlagBits aspect);
	const FormatInfo* FormatOf(VkFormat format, VkImageAspectFlagBits aspect);
}
