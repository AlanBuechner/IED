// The Vulkan records and commands serialized field by field (vk_records.h); Vulkan's own structures come from vk_gen.cpp.
#include "vk_records.h"

// Union views written as their 16 bytes; descriptions show both readings that matter.
void SerField(fb::Ser& s, const char* name, VkClearColorValue& v) {
	s.BeginStruct(name);
	s.Fixed("float32", v.float32);
	if (s.Describing()) {
		s.Json()->BeginArray("uint32");
		for (fb::u32 value : v.uint32) s.Json()->Uint(nullptr, value);
		s.Json()->EndArray();
	}
	s.EndStruct();
}

void SerField(fb::Ser& s, const char* name, VkClearValue& v) {
	s.BeginStruct(name);
	SerField(s, "color", v.color);
	if (s.Describing()) {
		s.Json()->BeginObject("depthStencil");
		s.Json()->Double("depth", v.depthStencil.depth);
		s.Json()->Uint("stencil", v.depthStencil.stencil);
		s.Json()->EndObject();
	}
	s.EndStruct();
}

void Serialize(fb::Ser& s, VkWriteDescriptorSet& v) {
	s.Field("sType", v.sType);
	fb::vk::SerializeNext(s, "pNext", v.pNext);
	s.Field("dstSet", v.dstSet);
	s.Field("dstBinding", v.dstBinding);
	s.Field("dstArrayElement", v.dstArrayElement);
	s.Field("descriptorCount", v.descriptorCount);
	s.Field("descriptorType", v.descriptorType);
	switch (v.descriptorType) {
		case VK_DESCRIPTOR_TYPE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			fb::vk::SerArray(s, "pImageInfo", v.pImageInfo, v.descriptorCount);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			fb::vk::SerArray(s, "pBufferInfo", v.pBufferInfo, v.descriptorCount);
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			fb::vk::SerArray(s, "pTexelBufferView", v.pTexelBufferView, v.descriptorCount);
			break;
		default: break;
	}
}

namespace fb::vk
{
	const char* ObjectKindName(ObjectKind kind) {
		static const char* names[] = {"Unknown", "Device", "Queue", "Memory", "Buffer", "Image", "ImageView", "BufferView", "Sampler",
			"ShaderModule", "DescriptorSetLayout", "PipelineLayout", "DescriptorPool", "DescriptorSet", "RenderPass", "Framebuffer",
			"Pipeline", "CommandPool", "CommandBuffer", "Fence", "Semaphore", "Event", "QueryPool", "Swapchain"};
		return (u32)kind < sizeof(names) / sizeof(names[0]) ? names[(u32)kind] : "Unknown";
	}

	ObjectKind KindOfRecord(u32 record) {
		switch (record) {
			case kDevice: return ObjectKind::Device;
			case kQueue: return ObjectKind::Queue;
			case kMemory: return ObjectKind::Memory;
			case kBuffer: return ObjectKind::Buffer;
			case kImage: return ObjectKind::Image;
			case kImageView: return ObjectKind::ImageView;
			case kBufferView: return ObjectKind::BufferView;
			case kSampler: return ObjectKind::Sampler;
			case kShaderModule: return ObjectKind::ShaderModule;
			case kDescriptorSetLayout: return ObjectKind::DescriptorSetLayout;
			case kPipelineLayout: return ObjectKind::PipelineLayout;
			case kDescriptorPool: return ObjectKind::DescriptorPool;
			case kDescriptorSet: return ObjectKind::DescriptorSet;
			case kRenderPass: return ObjectKind::RenderPass;
			case kFramebuffer: return ObjectKind::Framebuffer;
			case kGraphicsPipeline:
			case kComputePipeline:
				return ObjectKind::Pipeline;
			case kCommandPool: return ObjectKind::CommandPool;
			case kCommandBuffer: return ObjectKind::CommandBuffer;
			case kFence: return ObjectKind::Fence;
			case kSemaphore: return ObjectKind::Semaphore;
			case kEvent: return ObjectKind::Event;
			case kQueryPool: return ObjectKind::QueryPool;
			case kSwapchain: return ObjectKind::Swapchain;
			default: return ObjectKind::Unknown;
		}
	}

	// --- Records ------------------------------------------------------------------------------------------------

	void Serialize(Ser& s, MemoryTypeInfo& v) {
		s.Flags("propertyFlags", v.propertyFlags, &Flags_VkMemoryPropertyFlagBits);
		s.Field("heapIndex", v.heapIndex);
	}

	void Serialize(Ser& s, DeviceInfo& v) {
		s.Field("id", v.id);
		s.Field("instanceApiVersion", v.instanceApiVersion);
		s.Text("application", v.application);
		s.Text("engine", v.engine);
		s.Field("apiVersion", v.apiVersion);
		s.Field("driverVersion", v.driverVersion);
		s.Field("vendorId", v.vendorId);
		s.Field("deviceId", v.deviceId);
		s.Field("deviceType", v.deviceType);
		s.Text("deviceName", v.deviceName);
		s.Text("driverName", v.driverName);
		s.Text("driverInfo", v.driverInfo);
		s.Fixed("pipelineCacheUuid", v.pipelineCacheUuid);
		s.Field("memoryTypeCount", v.memoryTypeCount);
		SerArray(s, "memoryTypes", v.memoryTypes, v.memoryTypeCount);
		s.Field("instanceExtensionCount", v.instanceExtensionCount);
		s.StrArray("instanceExtensions", v.instanceExtensions, v.instanceExtensionCount);
		s.Field("create", v.create);
	}

	void Serialize(Ser& s, QueueCreate& v) {
		s.Field("id", v.id);
		s.Field("family", v.family);
		s.Field("index", v.index);
		s.Flags("familyFlags", v.familyFlags, &Flags_VkQueueFlagBits);
	}

	void Serialize(Ser& s, MemoryCreate& v) {
		s.Field("id", v.id);
		s.Field("info", v.info);
		s.Flags("propertyFlags", v.propertyFlags, &Flags_VkMemoryPropertyFlagBits);
	}

	void Serialize(Ser& s, BufferCreate& v) {
		s.Field("id", v.id);
		s.Field("info", v.info);
		s.Value("memory", v.memory);
		s.Field("memoryOffset", v.memoryOffset);
		s.Field("size", v.size);
	}

	void Serialize(Ser& s, ImageCreate& v) {
		s.Field("id", v.id);
		s.Field("info", v.info);
		s.Value("memory", v.memory);
		s.Field("memoryOffset", v.memoryOffset);
		s.Field("size", v.size);
		s.Value("swapchain", v.swapchain);
		s.Field("swapchainIndex", v.swapchainIndex);
	}

	void Serialize(Ser& s, DescriptorSetCreate& v) {
		s.Field("id", v.id);
		s.Value("pool", v.pool);
		s.Value("layout", v.layout);
		s.Field("variableCount", v.variableCount);
	}

	void Serialize(Ser& s, RenderPassCreate& v) {
		s.Field("id", v.id);
		s.Field("version", v.version);
		if (v.version == 2) s.Field("info", v.info2);
		else s.Field("info", v.info);
	}

	void Serialize(Ser& s, CommandBufferCreate& v) {
		s.Field("id", v.id);
		s.Value("pool", v.pool);
		s.Field("level", v.level);
	}

	void Serialize(Ser& s, SwapchainCreate& v) {
		s.Field("id", v.id);
		s.Field("info", v.info);
		s.Field("imageCount", v.imageCount);
		SerArray(s, "images", v.images, v.imageCount);
	}

	void Serialize(Ser& s, ObjectName& v) {
		s.Field("id", v.id);
		s.Text("name", v.name);
	}

	void Serialize(Ser& s, ObjectRef& v) { s.Field("id", v.id); }

	void Serialize(Ser& s, DescriptorUpdate& v) {
		s.Field("writeCount", v.writeCount);
		SerArray(s, "writes", v.writes, v.writeCount);
		s.Field("copyCount", v.copyCount);
		SerArray(s, "copies", v.copies, v.copyCount);
	}

	void Serialize(Ser& s, ImageLayout& v) {
		s.Value("image", v.image);
		s.Field("count", v.count);
		SerArray(s, "layouts", v.layouts, v.count);
	}

	void Serialize(Ser& s, MemoryContents& v) {
		s.Value("memory", v.memory);
		s.Field("offset", v.offset);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, BufferContents& v) {
		s.Value("buffer", v.buffer);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, ImageContents& v) {
		s.Value("image", v.image);
		s.Flags("aspect", v.aspect, &Flags_VkImageAspectFlagBits);
		s.Field("mip", v.mip);
		s.Field("layer", v.layer);
		s.Field("extent", v.extent);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, CommandBufferContents& v) {
		s.Value("commandBuffer", v.commandBuffer);
		s.Field("commandCount", v.commandCount);
		s.Blob("commands", v.commands, v.size);
	}

	void Serialize(Ser& s, SubmitBatch& v) {
		s.Field("waitCount", v.waitCount);
		SerArray(s, "waitSemaphores", v.waitSemaphores, v.waitCount);
		s.Field("commandBufferCount", v.commandBufferCount);
		SerArray(s, "commandBuffers", v.commandBuffers, v.commandBufferCount);
		s.Field("signalCount", v.signalCount);
		SerArray(s, "signalSemaphores", v.signalSemaphores, v.signalCount);
	}

	void Serialize(Ser& s, Submit& v) {
		s.Value("queue", v.queue);
		s.Field("batchCount", v.batchCount);
		SerArray(s, "batches", v.batches, v.batchCount);
		s.Value("fence", v.fence);
	}

	void Serialize(Ser& s, MapWrite& v) {
		s.Value("memory", v.memory);
		s.Field("offset", v.offset);
		s.Blob("data", v.data, v.size);
	}

	void Serialize(Ser& s, Present& v) {
		s.Value("queue", v.queue);
		s.Value("swapchain", v.swapchain);
		s.Field("imageIndex", v.imageIndex);
		s.Value("image", v.image);
	}

	void Serialize(Ser& s, QueueLabel& v) {
		s.Value("queue", v.queue);
		s.Field("op", v.op);
		s.Text("name", v.name);
		s.Fixed("color", v.color);
	}

	// --- Commands -----------------------------------------------------------------------------------------------

#define FB_VK_COMMAND_ARGS(X) \
	X(BindPipeline, BindPipelineArgs) X(SetViewport, ViewportArgs) X(SetScissor, ScissorArgs) X(SetLineWidth, FloatArgs) \
	X(SetDepthBias, DepthBiasArgs) X(SetBlendConstants, BlendConstantsArgs) X(SetDepthBounds, DepthBoundsArgs) \
	X(SetStencilCompareMask, StencilArgs) X(SetStencilWriteMask, StencilArgs) X(SetStencilReference, StencilArgs) \
	X(BindDescriptorSets, BindDescriptorSetsArgs) X(BindIndexBuffer, BindIndexBufferArgs) X(BindVertexBuffers, BindVertexBuffersArgs) \
	X(Draw, DrawArgs) X(DrawIndexed, DrawIndexedArgs) X(DrawIndirect, DrawIndirectArgs) X(DrawIndexedIndirect, DrawIndirectArgs) \
	X(DrawIndirectCount, DrawIndirectCountArgs) X(DrawIndexedIndirectCount, DrawIndirectCountArgs) X(Dispatch, DispatchArgs) \
	X(DispatchBase, DispatchArgs) X(DispatchIndirect, DispatchIndirectArgs) X(CopyBuffer, CopyBufferArgs) X(CopyImage, CopyImageArgs) \
	X(BlitImage, BlitImageArgs) X(CopyBufferToImage, CopyBufferToImageArgs) X(CopyImageToBuffer, CopyImageToBufferArgs) \
	X(UpdateBuffer, UpdateBufferArgs) X(FillBuffer, FillBufferArgs) X(ClearColorImage, ClearColorImageArgs) \
	X(ClearDepthStencilImage, ClearDepthStencilImageArgs) X(ClearAttachments, ClearAttachmentsArgs) X(ResolveImage, ResolveImageArgs) \
	X(CopyBuffer2, CopyBuffer2Args) X(CopyImage2, CopyImage2Args) X(BlitImage2, BlitImage2Args) \
	X(CopyBufferToImage2, CopyBufferToImage2Args) X(CopyImageToBuffer2, CopyImageToBuffer2Args) X(ResolveImage2, ResolveImage2Args) \
	X(PipelineBarrier, PipelineBarrierArgs) X(PipelineBarrier2, PipelineBarrier2Args) X(PushConstants, PushConstantsArgs) \
	X(BeginRenderPass, BeginRenderPassArgs) X(NextSubpass, NextSubpassArgs) X(EndRenderPass, EmptyArgs) \
	X(BeginRendering, BeginRenderingArgs) X(EndRendering, EmptyArgs) X(BeginLabel, LabelArgs) X(EndLabel, EmptyArgs) \
	X(InsertLabel, LabelArgs) X(ResetQueryPool, ResetQueryPoolArgs) X(BeginQuery, BeginQueryArgs) X(EndQuery, EndQueryArgs) \
	X(WriteTimestamp, WriteTimestampArgs) X(CopyQueryPoolResults, CopyQueryPoolResultsArgs) X(SetDynamic, SetDynamicArgs) \
	X(ExecuteCommands, ExecuteCommandsArgs) X(Unsupported, UnsupportedArgs)

	const char* CmdName(Cmd command) {
		switch (command) {
#define FB_NAME_ITEM(name) case Cmd::name: return #name;
			FB_VK_COMMANDS(FB_NAME_ITEM)
#undef FB_NAME_ITEM
			default: return "?";
		}
	}

	CmdClass ClassOf(Cmd command) {
		switch (command) {
			case Cmd::Draw:
			case Cmd::DrawIndexed:
			case Cmd::DrawIndirect:
			case Cmd::DrawIndexedIndirect:
			case Cmd::DrawIndirectCount:
			case Cmd::DrawIndexedIndirectCount:
				return CmdClass::Draw;
			case Cmd::Dispatch:
			case Cmd::DispatchBase:
			case Cmd::DispatchIndirect:
				return CmdClass::Dispatch;
			case Cmd::CopyBuffer:
			case Cmd::CopyImage:
			case Cmd::BlitImage:
			case Cmd::CopyBufferToImage:
			case Cmd::CopyImageToBuffer:
			case Cmd::UpdateBuffer:
			case Cmd::FillBuffer:
			case Cmd::CopyBuffer2:
			case Cmd::CopyImage2:
			case Cmd::BlitImage2:
			case Cmd::CopyBufferToImage2:
			case Cmd::CopyImageToBuffer2:
			case Cmd::CopyQueryPoolResults:
				return CmdClass::Copy;
			case Cmd::ClearColorImage:
			case Cmd::ClearDepthStencilImage:
			case Cmd::ClearAttachments:
				return CmdClass::Clear;
			case Cmd::ResolveImage:
			case Cmd::ResolveImage2:
				return CmdClass::Resolve;
			case Cmd::PipelineBarrier:
			case Cmd::PipelineBarrier2:
				return CmdClass::Barrier;
			case Cmd::BeginLabel:
			case Cmd::EndLabel:
			case Cmd::InsertLabel:
				return CmdClass::Marker;
			case Cmd::ResetQueryPool:
			case Cmd::BeginQuery:
			case Cmd::EndQuery:
			case Cmd::WriteTimestamp:
				return CmdClass::Query;
			case Cmd::ExecuteCommands:
			case Cmd::Unsupported:
				return CmdClass::Other;
			default: return CmdClass::State;
		}
	}

	bool IsAction(Cmd command) {
		const CmdClass c = ClassOf(command);
		return c == CmdClass::Draw || c == CmdClass::Dispatch || c == CmdClass::Copy || c == CmdClass::Clear || c == CmdClass::Resolve;
	}

	static void SerArgs(Ser& s, BindPipelineArgs& a) {
		s.Field("pipelineBindPoint", a.bindPoint);
		s.Field("pipeline", a.pipeline);
	}
	static void SerArgs(Ser& s, ViewportArgs& a) {
		s.Field("firstViewport", a.first);
		s.Field("viewportCount", a.count);
		SerArray(s, "pViewports", a.viewports, a.count);
	}
	static void SerArgs(Ser& s, ScissorArgs& a) {
		s.Field("firstScissor", a.first);
		s.Field("scissorCount", a.count);
		SerArray(s, "pScissors", a.scissors, a.count);
	}
	static void SerArgs(Ser& s, FloatArgs& a) { s.Field("value", a.value); }
	static void SerArgs(Ser& s, DepthBiasArgs& a) {
		s.Field("depthBiasConstantFactor", a.constant);
		s.Field("depthBiasClamp", a.clamp);
		s.Field("depthBiasSlopeFactor", a.slope);
	}
	static void SerArgs(Ser& s, BlendConstantsArgs& a) { s.Fixed("blendConstants", a.constants); }
	static void SerArgs(Ser& s, DepthBoundsArgs& a) {
		s.Field("minDepthBounds", a.min);
		s.Field("maxDepthBounds", a.max);
	}
	static void SerArgs(Ser& s, StencilArgs& a) {
		s.Flags("faceMask", a.faces, &Flags_VkStencilFaceFlagBits);
		s.Field("value", a.value);
	}
	static void SerArgs(Ser& s, BindDescriptorSetsArgs& a) {
		s.Field("pipelineBindPoint", a.bindPoint);
		s.Field("layout", a.layout);
		s.Field("firstSet", a.firstSet);
		s.Field("descriptorSetCount", a.setCount);
		SerArray(s, "pDescriptorSets", a.sets, a.setCount);
		s.Field("dynamicOffsetCount", a.dynamicOffsetCount);
		SerArray(s, "pDynamicOffsets", a.dynamicOffsets, a.dynamicOffsetCount);
	}
	static void SerArgs(Ser& s, BindIndexBufferArgs& a) {
		s.Field("buffer", a.buffer);
		s.Field("offset", a.offset);
		s.Field("size", a.size);
		s.Field("indexType", a.indexType);
	}
	static void SerArgs(Ser& s, BindVertexBuffersArgs& a) {
		s.Field("firstBinding", a.first);
		s.Field("bindingCount", a.count);
		SerArray(s, "pBuffers", a.buffers, a.count);
		SerArray(s, "pOffsets", a.offsets, a.count);
		SerArray(s, "pSizes", a.sizes, a.count);
		SerArray(s, "pStrides", a.strides, a.count);
	}
	static void SerArgs(Ser& s, DrawArgs& a) {
		s.Field("vertexCount", a.vertexCount);
		s.Field("instanceCount", a.instanceCount);
		s.Field("firstVertex", a.firstVertex);
		s.Field("firstInstance", a.firstInstance);
	}
	static void SerArgs(Ser& s, DrawIndexedArgs& a) {
		s.Field("indexCount", a.indexCount);
		s.Field("instanceCount", a.instanceCount);
		s.Field("firstIndex", a.firstIndex);
		s.Field("vertexOffset", a.vertexOffset);
		s.Field("firstInstance", a.firstInstance);
	}
	static void SerArgs(Ser& s, DrawIndirectArgs& a) {
		s.Field("buffer", a.buffer);
		s.Field("offset", a.offset);
		s.Field("drawCount", a.drawCount);
		s.Field("stride", a.stride);
	}
	static void SerArgs(Ser& s, DrawIndirectCountArgs& a) {
		s.Field("buffer", a.buffer);
		s.Field("offset", a.offset);
		s.Field("countBuffer", a.countBuffer);
		s.Field("countBufferOffset", a.countOffset);
		s.Field("maxDrawCount", a.maxDrawCount);
		s.Field("stride", a.stride);
	}
	static void SerArgs(Ser& s, DispatchArgs& a) {
		s.Field("baseGroupX", a.baseX);
		s.Field("baseGroupY", a.baseY);
		s.Field("baseGroupZ", a.baseZ);
		s.Field("groupCountX", a.x);
		s.Field("groupCountY", a.y);
		s.Field("groupCountZ", a.z);
	}
	static void SerArgs(Ser& s, DispatchIndirectArgs& a) {
		s.Field("buffer", a.buffer);
		s.Field("offset", a.offset);
	}
	static void SerArgs(Ser& s, CopyBufferArgs& a) {
		s.Field("srcBuffer", a.src);
		s.Field("dstBuffer", a.dst);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
	}
	static void SerArgs(Ser& s, CopyImageArgs& a) {
		s.Field("srcImage", a.src);
		s.Field("srcImageLayout", a.srcLayout);
		s.Field("dstImage", a.dst);
		s.Field("dstImageLayout", a.dstLayout);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
	}
	static void SerArgs(Ser& s, BlitImageArgs& a) {
		s.Field("srcImage", a.src);
		s.Field("srcImageLayout", a.srcLayout);
		s.Field("dstImage", a.dst);
		s.Field("dstImageLayout", a.dstLayout);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
		s.Field("filter", a.filter);
	}
	static void SerArgs(Ser& s, CopyBufferToImageArgs& a) {
		s.Field("srcBuffer", a.src);
		s.Field("dstImage", a.dst);
		s.Field("dstImageLayout", a.dstLayout);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
	}
	static void SerArgs(Ser& s, CopyImageToBufferArgs& a) {
		s.Field("srcImage", a.src);
		s.Field("srcImageLayout", a.srcLayout);
		s.Field("dstBuffer", a.dst);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
	}
	static void SerArgs(Ser& s, UpdateBufferArgs& a) {
		s.Field("dstBuffer", a.dst);
		s.Field("dstOffset", a.offset);
		s.Blob("pData", a.data, a.size);
	}
	static void SerArgs(Ser& s, FillBufferArgs& a) {
		s.Field("dstBuffer", a.dst);
		s.Field("dstOffset", a.offset);
		s.Field("size", a.size);
		s.Field("data", a.data);
	}
	static void SerArgs(Ser& s, ClearColorImageArgs& a) {
		s.Field("image", a.image);
		s.Field("imageLayout", a.layout);
		s.Field("color", a.color);
		s.Field("rangeCount", a.count);
		SerArray(s, "pRanges", a.ranges, a.count);
	}
	static void SerArgs(Ser& s, ClearDepthStencilImageArgs& a) {
		s.Field("image", a.image);
		s.Field("imageLayout", a.layout);
		s.Field("depthStencil", a.value);
		s.Field("rangeCount", a.count);
		SerArray(s, "pRanges", a.ranges, a.count);
	}
	static void SerArgs(Ser& s, ClearAttachmentsArgs& a) {
		s.Field("attachmentCount", a.attachmentCount);
		SerArray(s, "pAttachments", a.attachments, a.attachmentCount);
		s.Field("rectCount", a.rectCount);
		SerArray(s, "pRects", a.rects, a.rectCount);
	}
	static void SerArgs(Ser& s, ResolveImageArgs& a) {
		s.Field("srcImage", a.src);
		s.Field("srcImageLayout", a.srcLayout);
		s.Field("dstImage", a.dst);
		s.Field("dstImageLayout", a.dstLayout);
		s.Field("regionCount", a.count);
		SerArray(s, "pRegions", a.regions, a.count);
	}
	static void SerArgs(Ser& s, CopyBuffer2Args& a) { s.Optional("pCopyBufferInfo", a.info); }
	static void SerArgs(Ser& s, CopyImage2Args& a) { s.Optional("pCopyImageInfo", a.info); }
	static void SerArgs(Ser& s, BlitImage2Args& a) { s.Optional("pBlitImageInfo", a.info); }
	static void SerArgs(Ser& s, CopyBufferToImage2Args& a) { s.Optional("pCopyBufferToImageInfo", a.info); }
	static void SerArgs(Ser& s, CopyImageToBuffer2Args& a) { s.Optional("pCopyImageToBufferInfo", a.info); }
	static void SerArgs(Ser& s, ResolveImage2Args& a) { s.Optional("pResolveImageInfo", a.info); }
	static void SerArgs(Ser& s, PipelineBarrierArgs& a) {
		s.Flags("srcStageMask", a.srcStages, &Flags_VkPipelineStageFlagBits);
		s.Flags("dstStageMask", a.dstStages, &Flags_VkPipelineStageFlagBits);
		s.Flags("dependencyFlags", a.dependencyFlags, &Flags_VkDependencyFlagBits);
		s.Field("memoryBarrierCount", a.memoryCount);
		SerArray(s, "pMemoryBarriers", a.memory, a.memoryCount);
		s.Field("bufferMemoryBarrierCount", a.bufferCount);
		SerArray(s, "pBufferMemoryBarriers", a.buffers, a.bufferCount);
		s.Field("imageMemoryBarrierCount", a.imageCount);
		SerArray(s, "pImageMemoryBarriers", a.images, a.imageCount);
	}
	static void SerArgs(Ser& s, PipelineBarrier2Args& a) { s.Optional("pDependencyInfo", a.info); }
	static void SerArgs(Ser& s, PushConstantsArgs& a) {
		s.Field("layout", a.layout);
		s.Flags("stageFlags", a.stages, &Flags_VkShaderStageFlagBits);
		s.Field("offset", a.offset);
		s.Blob("pValues", a.values, a.size);
	}
	static void SerArgs(Ser& s, BeginRenderPassArgs& a) {
		s.Optional("pRenderPassBegin", a.begin);
		s.Field("contents", a.contents);
	}
	static void SerArgs(Ser& s, NextSubpassArgs& a) { s.Field("contents", a.contents); }
	static void SerArgs(Ser& s, EmptyArgs& a) {}
	static void SerArgs(Ser& s, BeginRenderingArgs& a) { s.Optional("pRenderingInfo", a.info); }
	static void SerArgs(Ser& s, LabelArgs& a) {
		s.Str("pLabelName", a.name);
		s.Fixed("color", a.color);
	}
	static void SerArgs(Ser& s, ResetQueryPoolArgs& a) {
		s.Field("queryPool", a.pool);
		s.Field("firstQuery", a.first);
		s.Field("queryCount", a.count);
	}
	static void SerArgs(Ser& s, BeginQueryArgs& a) {
		s.Field("queryPool", a.pool);
		s.Field("query", a.query);
		s.Flags("flags", a.flags, &Flags_VkQueryControlFlagBits);
	}
	static void SerArgs(Ser& s, EndQueryArgs& a) {
		s.Field("queryPool", a.pool);
		s.Field("query", a.query);
	}
	static void SerArgs(Ser& s, WriteTimestampArgs& a) {
		s.Flags("stage", a.stage, &Flags_VkPipelineStageFlagBits2);
		s.Field("queryPool", a.pool);
		s.Field("query", a.query);
	}
	static void SerArgs(Ser& s, CopyQueryPoolResultsArgs& a) {
		s.Field("queryPool", a.pool);
		s.Field("firstQuery", a.first);
		s.Field("queryCount", a.count);
		s.Field("dstBuffer", a.dst);
		s.Field("dstOffset", a.offset);
		s.Field("stride", a.stride);
		s.Flags("flags", a.flags, &Flags_VkQueryResultFlagBits);
	}
	static void SerArgs(Ser& s, SetDynamicArgs& a) {
		s.Field("state", a.state);
		s.Fixed("values", a.values);
	}
	static void SerArgs(Ser& s, ExecuteCommandsArgs& a) { s.Value("commandBuffer", a.commandBuffer); }
	static void SerArgs(Ser& s, UnsupportedArgs& a) { s.Str("name", a.name); }

	void SerializeCommand(Ser& s, Cmd command, void* raw) {
		switch (command) {
#define FB_SER_ITEM(name, type) case Cmd::name: SerArgs(s, *(type*)raw); return;
			FB_VK_COMMAND_ARGS(FB_SER_ITEM)
#undef FB_SER_ITEM
			default: return;
		}
	}

	size_t CommandArgsSize(Cmd command) {
		switch (command) {
#define FB_SIZE_ITEM(name, type) case Cmd::name: return sizeof(type);
			FB_VK_COMMAND_ARGS(FB_SIZE_ITEM)
#undef FB_SIZE_ITEM
			default: return 0;
		}
	}
}
