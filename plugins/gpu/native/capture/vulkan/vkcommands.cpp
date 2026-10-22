// The layer's command hooks: each command calls down, then is written to its command buffer's recording (handles as
// ids), with the image layout changes of barriers and render passes noted for when the buffer is submitted. And the
// queue hooks: submissions (layouts applied, the frame's record when capturing), presents (the frame boundary) and
// queue labels.
#include "vklayer.h"

#include <cstring>

namespace fb::capture::vulkan
{
	PFN_vkVoidFunction ObjectHook(const char* name);

	static Recording* RecordingOf(Device& device, VkCommandBuffer cb) {
		Object* object = device.Find(cb);
		return object && object->recording ? object->recording.get() : nullptr;
	}

	template <class A> static void Rec(Device* device, VkCommandBuffer cb, Cmd command, A& args, u16 flags = 0) {
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Recording* recording = RecordingOf(*device, cb);
		if (!recording) return;
		WriteCommand(recording->commands, device, command, args, flags);
		recording->count += 1;
		for (u32 type : TakeDroppedNext()) recording->droppedNext.push_back(type);
	}

	static void Unsupported(Device* device, VkCommandBuffer cb, const char* name) {
		UnsupportedArgs args = {name};
		Rec(device, cb, Cmd::Unsupported, args);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		if (Recording* recording = RecordingOf(*device, cb)) {
			for (const std::string& known : recording->unsupported)
				if (known == name) return;
			recording->unsupported.push_back(name);
		}
	}

	static void NoteLayout(Device* device, VkCommandBuffer cb, VkImage image, const VkImageSubresourceRange& range, VkImageLayout layout) {
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Recording* recording = RecordingOf(*device, cb);
		Object* object = device->Find(image);
		if (recording && object) recording->layouts.push_back({object->id, range, layout});
	}

#define FB_DEVICE Device* device = DeviceOf(cb)

	// --- State ---------------------------------------------------------------------------------------------------------

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bindPoint, VkPipeline pipeline) {
		FB_DEVICE;
		device->next.vkCmdBindPipeline(cb, bindPoint, pipeline);
		BindPipelineArgs args = {bindPoint, pipeline};
		Rec(device, cb, Cmd::BindPipeline, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkViewport* viewports) {
		FB_DEVICE;
		device->next.vkCmdSetViewport(cb, first, count, viewports);
		ViewportArgs args = {first, count, viewports};
		Rec(device, cb, Cmd::SetViewport, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkRect2D* scissors) {
		FB_DEVICE;
		device->next.vkCmdSetScissor(cb, first, count, scissors);
		ScissorArgs args = {first, count, scissors};
		Rec(device, cb, Cmd::SetScissor, args);
	}

	// vkCmdSetViewportWithCount and vkCmdSetScissorWithCount: the same arguments, flags 1.
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count, const VkViewport* viewports) {
		FB_DEVICE;
		device->next.vkCmdSetViewportWithCount(cb, count, viewports);
		ViewportArgs args = {0, count, viewports};
		Rec(device, cb, Cmd::SetViewport, args, 1);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetScissorWithCount(VkCommandBuffer cb, uint32_t count, const VkRect2D* scissors) {
		FB_DEVICE;
		device->next.vkCmdSetScissorWithCount(cb, count, scissors);
		ScissorArgs args = {0, count, scissors};
		Rec(device, cb, Cmd::SetScissor, args, 1);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetLineWidth(VkCommandBuffer cb, float width) {
		FB_DEVICE;
		device->next.vkCmdSetLineWidth(cb, width);
		FloatArgs args = {width};
		Rec(device, cb, Cmd::SetLineWidth, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBias(VkCommandBuffer cb, float constant, float clamp, float slope) {
		FB_DEVICE;
		device->next.vkCmdSetDepthBias(cb, constant, clamp, slope);
		DepthBiasArgs args = {constant, clamp, slope};
		Rec(device, cb, Cmd::SetDepthBias, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetBlendConstants(VkCommandBuffer cb, const float constants[4]) {
		FB_DEVICE;
		device->next.vkCmdSetBlendConstants(cb, constants);
		BlendConstantsArgs args;
		memcpy(args.constants, constants, sizeof(args.constants));
		Rec(device, cb, Cmd::SetBlendConstants, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetDepthBounds(VkCommandBuffer cb, float min, float max) {
		FB_DEVICE;
		device->next.vkCmdSetDepthBounds(cb, min, max);
		DepthBoundsArgs args = {min, max};
		Rec(device, cb, Cmd::SetDepthBounds, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilCompareMask(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value) {
		FB_DEVICE;
		device->next.vkCmdSetStencilCompareMask(cb, faces, value);
		StencilArgs args = {faces, value};
		Rec(device, cb, Cmd::SetStencilCompareMask, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilWriteMask(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value) {
		FB_DEVICE;
		device->next.vkCmdSetStencilWriteMask(cb, faces, value);
		StencilArgs args = {faces, value};
		Rec(device, cb, Cmd::SetStencilWriteMask, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilReference(VkCommandBuffer cb, VkStencilFaceFlags faces, uint32_t value) {
		FB_DEVICE;
		device->next.vkCmdSetStencilReference(cb, faces, value);
		StencilArgs args = {faces, value};
		Rec(device, cb, Cmd::SetStencilReference, args);
	}

	static void Dynamic(Device* device, VkCommandBuffer cb, VkDynamicState state, std::initializer_list<u32> values) {
		SetDynamicArgs args = {state, {}};
		u32 i = 0;
		for (u32 value : values) args.values[i++] = value;
		Rec(device, cb, Cmd::SetDynamic, args);
	}

#define FB_DYNAMIC1(fn, state, type) \
	static VKAPI_ATTR void VKAPI_CALL hook_##fn(VkCommandBuffer cb, type value) { \
		FB_DEVICE; \
		device->next.fn(cb, value); \
		Dynamic(device, cb, state, {(u32)value}); \
	}
	FB_DYNAMIC1(vkCmdSetCullMode, VK_DYNAMIC_STATE_CULL_MODE, VkCullModeFlags)
	FB_DYNAMIC1(vkCmdSetFrontFace, VK_DYNAMIC_STATE_FRONT_FACE, VkFrontFace)
	FB_DYNAMIC1(vkCmdSetPrimitiveTopology, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VkPrimitiveTopology)
	FB_DYNAMIC1(vkCmdSetDepthTestEnable, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetDepthWriteEnable, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetDepthCompareOp, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VkCompareOp)
	FB_DYNAMIC1(vkCmdSetDepthBoundsTestEnable, VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetStencilTestEnable, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetRasterizerDiscardEnable, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetDepthBiasEnable, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE, VkBool32)
	FB_DYNAMIC1(vkCmdSetPrimitiveRestartEnable, VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE, VkBool32)
#undef FB_DYNAMIC1

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags faces, VkStencilOp fail, VkStencilOp pass, VkStencilOp depthFail, VkCompareOp compare) {
		FB_DEVICE;
		device->next.vkCmdSetStencilOp(cb, faces, fail, pass, depthFail, compare);
		Dynamic(device, cb, VK_DYNAMIC_STATE_STENCIL_OP, {(u32)faces, (u32)fail, (u32)pass, (u32)depthFail, (u32)compare});
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t firstSet,
		uint32_t setCount, const VkDescriptorSet* sets, uint32_t dynamicCount, const uint32_t* dynamicOffsets) {
		FB_DEVICE;
		device->next.vkCmdBindDescriptorSets(cb, bindPoint, layout, firstSet, setCount, sets, dynamicCount, dynamicOffsets);
		BindDescriptorSetsArgs args = {bindPoint, layout, firstSet, setCount, sets, dynamicCount, dynamicOffsets};
		Rec(device, cb, Cmd::BindDescriptorSets, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
		FB_DEVICE;
		device->next.vkCmdBindIndexBuffer(cb, buffer, offset, type);
		BindIndexBufferArgs args = {buffer, offset, VK_WHOLE_SIZE, type};
		Rec(device, cb, Cmd::BindIndexBuffer, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindIndexBuffer2KHR(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType type) {
		FB_DEVICE;
		device->next.vkCmdBindIndexBuffer2KHR(cb, buffer, offset, size, type);
		BindIndexBufferArgs args = {buffer, offset, size, type};
		Rec(device, cb, Cmd::BindIndexBuffer, args, 1);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkBuffer* buffers, const VkDeviceSize* offsets) {
		FB_DEVICE;
		device->next.vkCmdBindVertexBuffers(cb, first, count, buffers, offsets);
		BindVertexBuffersArgs args = {first, count, buffers, offsets, nullptr, nullptr};
		Rec(device, cb, Cmd::BindVertexBuffers, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBindVertexBuffers2(VkCommandBuffer cb, uint32_t first, uint32_t count, const VkBuffer* buffers, const VkDeviceSize* offsets,
		const VkDeviceSize* sizes, const VkDeviceSize* strides) {
		FB_DEVICE;
		device->next.vkCmdBindVertexBuffers2(cb, first, count, buffers, offsets, sizes, strides);
		BindVertexBuffersArgs args = {first, count, buffers, offsets, sizes, strides};
		Rec(device, cb, Cmd::BindVertexBuffers, args, 1);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* values) {
		FB_DEVICE;
		device->next.vkCmdPushConstants(cb, layout, stages, offset, size, values);
		PushConstantsArgs args = {layout, stages, offset, size, values};
		Rec(device, cb, Cmd::PushConstants, args);
	}

	// --- Draws, dispatches, copies, clears -------------------------------------------------------------------------

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDraw(VkCommandBuffer cb, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
		FB_DEVICE;
		device->next.vkCmdDraw(cb, vertexCount, instanceCount, firstVertex, firstInstance);
		DrawArgs args = {vertexCount, instanceCount, firstVertex, firstInstance};
		Rec(device, cb, Cmd::Draw, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexed(VkCommandBuffer cb, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
		FB_DEVICE;
		device->next.vkCmdDrawIndexed(cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
		DrawIndexedArgs args = {indexCount, instanceCount, firstIndex, vertexOffset, firstInstance};
		Rec(device, cb, Cmd::DrawIndexed, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t count, uint32_t stride) {
		FB_DEVICE;
		device->next.vkCmdDrawIndirect(cb, buffer, offset, count, stride);
		DrawIndirectArgs args = {buffer, offset, count, stride};
		Rec(device, cb, Cmd::DrawIndirect, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, uint32_t count, uint32_t stride) {
		FB_DEVICE;
		device->next.vkCmdDrawIndexedIndirect(cb, buffer, offset, count, stride);
		DrawIndirectArgs args = {buffer, offset, count, stride};
		Rec(device, cb, Cmd::DrawIndexedIndirect, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countOffset,
		uint32_t maxCount, uint32_t stride) {
		FB_DEVICE;
		device->next.vkCmdDrawIndirectCount(cb, buffer, offset, countBuffer, countOffset, maxCount, stride);
		DrawIndirectCountArgs args = {buffer, offset, countBuffer, countOffset, maxCount, stride};
		Rec(device, cb, Cmd::DrawIndirectCount, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawIndexedIndirectCount(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countOffset,
		uint32_t maxCount, uint32_t stride) {
		FB_DEVICE;
		device->next.vkCmdDrawIndexedIndirectCount(cb, buffer, offset, countBuffer, countOffset, maxCount, stride);
		DrawIndirectCountArgs args = {buffer, offset, countBuffer, countOffset, maxCount, stride};
		Rec(device, cb, Cmd::DrawIndexedIndirectCount, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z) {
		FB_DEVICE;
		device->next.vkCmdDispatch(cb, x, y, z);
		DispatchArgs args = {0, 0, 0, x, y, z};
		Rec(device, cb, Cmd::Dispatch, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatchBase(VkCommandBuffer cb, uint32_t bx, uint32_t by, uint32_t bz, uint32_t x, uint32_t y, uint32_t z) {
		FB_DEVICE;
		device->next.vkCmdDispatchBase(cb, bx, by, bz, x, y, z);
		DispatchArgs args = {bx, by, bz, x, y, z};
		Rec(device, cb, Cmd::DispatchBase, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buffer, VkDeviceSize offset) {
		FB_DEVICE;
		device->next.vkCmdDispatchIndirect(cb, buffer, offset);
		DispatchIndirectArgs args = {buffer, offset};
		Rec(device, cb, Cmd::DispatchIndirect, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst, uint32_t count, const VkBufferCopy* regions) {
		FB_DEVICE;
		device->next.vkCmdCopyBuffer(cb, src, dst, count, regions);
		CopyBufferArgs args = {src, dst, count, regions};
		Rec(device, cb, Cmd::CopyBuffer, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, uint32_t count, const VkImageCopy* regions) {
		FB_DEVICE;
		device->next.vkCmdCopyImage(cb, src, srcLayout, dst, dstLayout, count, regions);
		CopyImageArgs args = {src, srcLayout, dst, dstLayout, count, regions};
		Rec(device, cb, Cmd::CopyImage, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, uint32_t count,
		const VkImageBlit* regions, VkFilter filter) {
		FB_DEVICE;
		device->next.vkCmdBlitImage(cb, src, srcLayout, dst, dstLayout, count, regions, filter);
		BlitImageArgs args = {src, srcLayout, dst, dstLayout, count, regions, filter};
		Rec(device, cb, Cmd::BlitImage, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, VkImageLayout dstLayout, uint32_t count, const VkBufferImageCopy* regions) {
		FB_DEVICE;
		device->next.vkCmdCopyBufferToImage(cb, src, dst, dstLayout, count, regions);
		CopyBufferToImageArgs args = {src, dst, dstLayout, count, regions};
		Rec(device, cb, Cmd::CopyBufferToImage, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkBuffer dst, uint32_t count, const VkBufferImageCopy* regions) {
		FB_DEVICE;
		device->next.vkCmdCopyImageToBuffer(cb, src, srcLayout, dst, count, regions);
		CopyImageToBufferArgs args = {src, srcLayout, dst, count, regions};
		Rec(device, cb, Cmd::CopyImageToBuffer, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, const void* data) {
		FB_DEVICE;
		device->next.vkCmdUpdateBuffer(cb, dst, offset, size, data);
		UpdateBufferArgs args = {dst, offset, size, data};
		Rec(device, cb, Cmd::UpdateBuffer, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, uint32_t data) {
		FB_DEVICE;
		device->next.vkCmdFillBuffer(cb, dst, offset, size, data);
		FillBufferArgs args = {dst, offset, size, data};
		Rec(device, cb, Cmd::FillBuffer, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearColorImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout, const VkClearColorValue* color, uint32_t count,
		const VkImageSubresourceRange* ranges) {
		FB_DEVICE;
		device->next.vkCmdClearColorImage(cb, image, layout, color, count, ranges);
		ClearColorImageArgs args = {image, layout, *color, count, ranges};
		Rec(device, cb, Cmd::ClearColorImage, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, VkImageLayout layout, const VkClearDepthStencilValue* value, uint32_t count,
		const VkImageSubresourceRange* ranges) {
		FB_DEVICE;
		device->next.vkCmdClearDepthStencilImage(cb, image, layout, value, count, ranges);
		ClearDepthStencilImageArgs args = {image, layout, *value, count, ranges};
		Rec(device, cb, Cmd::ClearDepthStencilImage, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdClearAttachments(VkCommandBuffer cb, uint32_t attachmentCount, const VkClearAttachment* attachments, uint32_t rectCount, const VkClearRect* rects) {
		FB_DEVICE;
		device->next.vkCmdClearAttachments(cb, attachmentCount, attachments, rectCount, rects);
		ClearAttachmentsArgs args = {attachmentCount, attachments, rectCount, rects};
		Rec(device, cb, Cmd::ClearAttachments, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdResolveImage(VkCommandBuffer cb, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, uint32_t count,
		const VkImageResolve* regions) {
		FB_DEVICE;
		device->next.vkCmdResolveImage(cb, src, srcLayout, dst, dstLayout, count, regions);
		ResolveImageArgs args = {src, srcLayout, dst, dstLayout, count, regions};
		Rec(device, cb, Cmd::ResolveImage, args);
	}

#define FB_INFO2(fn, cmd, type, Args) \
	static VKAPI_ATTR void VKAPI_CALL hook_##fn(VkCommandBuffer cb, const type* info) { \
		FB_DEVICE; \
		device->next.fn(cb, info); \
		Args args = {info}; \
		Rec(device, cb, Cmd::cmd, args); \
	}
	FB_INFO2(vkCmdCopyBuffer2, CopyBuffer2, VkCopyBufferInfo2, CopyBuffer2Args)
	FB_INFO2(vkCmdCopyImage2, CopyImage2, VkCopyImageInfo2, CopyImage2Args)
	FB_INFO2(vkCmdBlitImage2, BlitImage2, VkBlitImageInfo2, BlitImage2Args)
	FB_INFO2(vkCmdCopyBufferToImage2, CopyBufferToImage2, VkCopyBufferToImageInfo2, CopyBufferToImage2Args)
	FB_INFO2(vkCmdCopyImageToBuffer2, CopyImageToBuffer2, VkCopyImageToBufferInfo2, CopyImageToBuffer2Args)
	FB_INFO2(vkCmdResolveImage2, ResolveImage2, VkResolveImageInfo2, ResolveImage2Args)
#undef FB_INFO2

	// --- Barriers and passes -------------------------------------------------------------------------------------

	static void RecordBarrier(Device* device, VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags, uint32_t memoryCount,
		const VkMemoryBarrier* memory, uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, uint32_t imageCount, const VkImageMemoryBarrier* images) {
		PipelineBarrierArgs args = {src, dst, flags, memoryCount, memory, bufferCount, buffers, imageCount, images};
		Rec(device, cb, Cmd::PipelineBarrier, args);
		for (u32 i = 0; i < imageCount; ++i) NoteLayout(device, cb, images[i].image, images[i].subresourceRange, images[i].newLayout);
	}

	static void RecordBarrier2(Device* device, VkCommandBuffer cb, const VkDependencyInfo* info) {
		PipelineBarrier2Args args = {info};
		Rec(device, cb, Cmd::PipelineBarrier2, args);
		for (u32 i = 0; i < info->imageMemoryBarrierCount; ++i)
			NoteLayout(device, cb, info->pImageMemoryBarriers[i].image, info->pImageMemoryBarriers[i].subresourceRange, info->pImageMemoryBarriers[i].newLayout);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags flags, uint32_t memoryCount,
		const VkMemoryBarrier* memory, uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, uint32_t imageCount, const VkImageMemoryBarrier* images) {
		FB_DEVICE;
		device->next.vkCmdPipelineBarrier(cb, src, dst, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
		RecordBarrier(device, cb, src, dst, flags, memoryCount, memory, bufferCount, buffers, imageCount, images);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* info) {
		FB_DEVICE;
		device->next.vkCmdPipelineBarrier2(cb, info);
		RecordBarrier2(device, cb, info);
	}

	// Events: a replay runs serialized, so waiting for an event is its barriers; setting and resetting are nothing.
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdWaitEvents(VkCommandBuffer cb, uint32_t eventCount, const VkEvent* events, VkPipelineStageFlags src, VkPipelineStageFlags dst,
		uint32_t memoryCount, const VkMemoryBarrier* memory, uint32_t bufferCount, const VkBufferMemoryBarrier* buffers, uint32_t imageCount, const VkImageMemoryBarrier* images) {
		FB_DEVICE;
		device->next.vkCmdWaitEvents(cb, eventCount, events, src, dst, memoryCount, memory, bufferCount, buffers, imageCount, images);
		RecordBarrier(device, cb, src, dst, 0, memoryCount, memory, bufferCount, buffers, imageCount, images);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdWaitEvents2(VkCommandBuffer cb, uint32_t eventCount, const VkEvent* events, const VkDependencyInfo* infos) {
		FB_DEVICE;
		device->next.vkCmdWaitEvents2(cb, eventCount, events, infos);
		for (u32 i = 0; i < eventCount; ++i) RecordBarrier2(device, cb, &infos[i]);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stage) { DeviceOf(cb)->next.vkCmdSetEvent(cb, event, stage); }
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags stage) { DeviceOf(cb)->next.vkCmdResetEvent(cb, event, stage); }
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdSetEvent2(VkCommandBuffer cb, VkEvent event, const VkDependencyInfo* info) { DeviceOf(cb)->next.vkCmdSetEvent2(cb, event, info); }
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetEvent2(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags2 stage) { DeviceOf(cb)->next.vkCmdResetEvent2(cb, event, stage); }

	static void BeginPass(Device* device, VkCommandBuffer cb, const VkRenderPassBeginInfo* begin, VkSubpassContents contents) {
		BeginRenderPassArgs args = {begin, contents};
		Rec(device, cb, Cmd::BeginRenderPass, args);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Recording* recording = RecordingOf(*device, cb);
		if (!recording) return;
		Object* pass = device->Find(begin->renderPass);
		Object* framebuffer = device->Find(begin->framebuffer);
		recording->renderPass = pass ? pass->id : 0;
		recording->framebuffer = framebuffer ? framebuffer->id : 0;
		recording->attachments.clear();
		for (const VkBaseInStructure* next = (const VkBaseInStructure*)begin->pNext; next; next = next->pNext)
			if (next->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
				auto* views = (const VkRenderPassAttachmentBeginInfo*)next;
				for (u32 i = 0; i < views->attachmentCount; ++i) {
					Object* view = device->Find(views->pAttachments[i]);
					recording->attachments.push_back(view ? view->id : 0);
				}
			}
	}

	// At a pass's end its attachments are in their final layouts.
	static void EndPass(Device* device, VkCommandBuffer cb) {
		EmptyArgs args = {};
		Rec(device, cb, Cmd::EndRenderPass, args);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Recording* recording = RecordingOf(*device, cb);
		if (!recording) return;
		Object* pass = device->FindId(recording->renderPass);
		Object* framebuffer = device->FindId(recording->framebuffer);
		if (!pass) return;
		const std::vector<u64>& views = recording->attachments.empty() && framebuffer ? framebuffer->attachments : recording->attachments;
		for (size_t i = 0; i < views.size() && i < pass->finalLayouts.size(); ++i) {
			Object* view = device->FindId(views[i]);
			if (view && view->viewImage) recording->layouts.push_back({view->viewImage, view->viewRange, pass->finalLayouts[i]});
		}
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo* begin, VkSubpassContents contents) {
		FB_DEVICE;
		device->next.vkCmdBeginRenderPass(cb, begin, contents);
		BeginPass(device, cb, begin, contents);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRenderPass2(VkCommandBuffer cb, const VkRenderPassBeginInfo* begin, const VkSubpassBeginInfo* subpass) {
		FB_DEVICE;
		device->next.vkCmdBeginRenderPass2(cb, begin, subpass);
		BeginPass(device, cb, begin, subpass->contents);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdNextSubpass(VkCommandBuffer cb, VkSubpassContents contents) {
		FB_DEVICE;
		device->next.vkCmdNextSubpass(cb, contents);
		NextSubpassArgs args = {contents};
		Rec(device, cb, Cmd::NextSubpass, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdNextSubpass2(VkCommandBuffer cb, const VkSubpassBeginInfo* begin, const VkSubpassEndInfo* end) {
		FB_DEVICE;
		device->next.vkCmdNextSubpass2(cb, begin, end);
		NextSubpassArgs args = {begin->contents};
		Rec(device, cb, Cmd::NextSubpass, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass(VkCommandBuffer cb) {
		FB_DEVICE;
		device->next.vkCmdEndRenderPass(cb);
		EndPass(device, cb);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRenderPass2(VkCommandBuffer cb, const VkSubpassEndInfo* end) {
		FB_DEVICE;
		device->next.vkCmdEndRenderPass2(cb, end);
		EndPass(device, cb);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo* info) {
		FB_DEVICE;
		device->next.vkCmdBeginRendering(cb, info);
		BeginRenderingArgs args = {info};
		Rec(device, cb, Cmd::BeginRendering, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndRendering(VkCommandBuffer cb) {
		FB_DEVICE;
		device->next.vkCmdEndRendering(cb);
		EmptyArgs args = {};
		Rec(device, cb, Cmd::EndRendering, args);
	}

	// --- Labels, queries, secondary buffers --------------------------------------------------------------------------

	static void Label(Device* device, VkCommandBuffer cb, Cmd command, const VkDebugUtilsLabelEXT* label) {
		LabelArgs args = {label ? label->pLabelName : nullptr, {}};
		if (label) memcpy(args.color, label->color, sizeof(args.color));
		Rec(device, cb, command, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer cb, const VkDebugUtilsLabelEXT* label) {
		FB_DEVICE;
		if (device->next.vkCmdBeginDebugUtilsLabelEXT) device->next.vkCmdBeginDebugUtilsLabelEXT(cb, label);
		Label(device, cb, Cmd::BeginLabel, label);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer cb) {
		FB_DEVICE;
		if (device->next.vkCmdEndDebugUtilsLabelEXT) device->next.vkCmdEndDebugUtilsLabelEXT(cb);
		EmptyArgs args = {};
		Rec(device, cb, Cmd::EndLabel, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer cb, const VkDebugUtilsLabelEXT* label) {
		FB_DEVICE;
		if (device->next.vkCmdInsertDebugUtilsLabelEXT) device->next.vkCmdInsertDebugUtilsLabelEXT(cb, label);
		Label(device, cb, Cmd::InsertLabel, label);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count) {
		FB_DEVICE;
		device->next.vkCmdResetQueryPool(cb, pool, first, count);
		ResetQueryPoolArgs args = {pool, first, count};
		Rec(device, cb, Cmd::ResetQueryPool, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query, VkQueryControlFlags flags) {
		FB_DEVICE;
		device->next.vkCmdBeginQuery(cb, pool, query, flags);
		BeginQueryArgs args = {pool, query, flags};
		Rec(device, cb, Cmd::BeginQuery, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query) {
		FB_DEVICE;
		device->next.vkCmdEndQuery(cb, pool, query);
		EndQueryArgs args = {pool, query};
		Rec(device, cb, Cmd::EndQuery, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteTimestamp(VkCommandBuffer cb, VkPipelineStageFlagBits stage, VkQueryPool pool, uint32_t query) {
		FB_DEVICE;
		device->next.vkCmdWriteTimestamp(cb, stage, pool, query);
		WriteTimestampArgs args = {(u64)stage, pool, query};
		Rec(device, cb, Cmd::WriteTimestamp, args);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdWriteTimestamp2(VkCommandBuffer cb, VkPipelineStageFlags2 stage, VkQueryPool pool, uint32_t query) {
		FB_DEVICE;
		device->next.vkCmdWriteTimestamp2(cb, stage, pool, query);
		WriteTimestampArgs args = {(u64)stage, pool, query};
		Rec(device, cb, Cmd::WriteTimestamp, args, 1);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count, VkBuffer dst, VkDeviceSize offset,
		VkDeviceSize stride, VkQueryResultFlags flags) {
		FB_DEVICE;
		device->next.vkCmdCopyQueryPoolResults(cb, pool, first, count, dst, offset, stride, flags);
		CopyQueryPoolResultsArgs args = {pool, first, count, dst, offset, stride, flags};
		Rec(device, cb, Cmd::CopyQueryPoolResults, args);
	}

	// A secondary buffer's commands are inlined in the primary, between two ExecuteCommands markers (flags 1 ends).
	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdExecuteCommands(VkCommandBuffer cb, uint32_t count, const VkCommandBuffer* buffers) {
		FB_DEVICE;
		device->next.vkCmdExecuteCommands(cb, count, buffers);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Recording* recording = RecordingOf(*device, cb);
		if (!recording) return;
		for (u32 i = 0; i < count; ++i) {
			Object* secondary = device->Find(buffers[i]);
			if (!secondary || !secondary->recording) continue;
			ExecuteCommandsArgs args = {secondary->id};
			WriteCommand(recording->commands, device, Cmd::ExecuteCommands, args);
			const Recording& inner = *secondary->recording;
			recording->commands.Bytes(inner.commands.Data(), inner.commands.Size());
			WriteCommand(recording->commands, device, Cmd::ExecuteCommands, args, 1);
			recording->count += inner.count + 2;
			recording->layouts.insert(recording->layouts.end(), inner.layouts.begin(), inner.layouts.end());
			recording->unsupported.insert(recording->unsupported.end(), inner.unsupported.begin(), inner.unsupported.end());
		}
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdPushDescriptorSetKHR(VkCommandBuffer cb, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t set, uint32_t count,
		const VkWriteDescriptorSet* writes) {
		FB_DEVICE;
		device->next.vkCmdPushDescriptorSetKHR(cb, bindPoint, layout, set, count, writes);
		Unsupported(device, cb, "vkCmdPushDescriptorSetKHR");
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdDrawMeshTasksEXT(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z) {
		FB_DEVICE;
		device->next.vkCmdDrawMeshTasksEXT(cb, x, y, z);
		Unsupported(device, cb, "vkCmdDrawMeshTasksEXT");
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkCmdTraceRaysKHR(VkCommandBuffer cb, const VkStridedDeviceAddressRegionKHR* raygen, const VkStridedDeviceAddressRegionKHR* miss,
		const VkStridedDeviceAddressRegionKHR* hit, const VkStridedDeviceAddressRegionKHR* callable, uint32_t width, uint32_t height, uint32_t depth) {
		FB_DEVICE;
		device->next.vkCmdTraceRaysKHR(cb, raygen, miss, hit, callable, width, height, depth);
		Unsupported(device, cb, "vkCmdTraceRaysKHR");
	}

#undef FB_DEVICE

	// --- Queues ----------------------------------------------------------------------------------------------------

	static void Submitted(Device* device, VkQueue queue, const std::vector<std::vector<VkCommandBuffer>>& batches, const std::vector<std::vector<VkSemaphore>>& waits,
		const std::vector<std::vector<VkSemaphore>>& signals, VkFence fence) {
		if (device->frames.Active()) device->frames.BeforeSubmit(queue, batches, waits, signals, fence);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		for (const auto& batch : batches)
			for (VkCommandBuffer cb : batch)
				if (Recording* recording = RecordingOf(*device, cb)) ApplyLayouts(*device, *recording);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkQueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
		Device* device = DeviceOf(queue);
		std::vector<std::vector<VkCommandBuffer>> batches(count);
		std::vector<std::vector<VkSemaphore>> waits(count), signals(count);
		for (u32 i = 0; i < count; ++i) {
			batches[i].assign(submits[i].pCommandBuffers, submits[i].pCommandBuffers + submits[i].commandBufferCount);
			waits[i].assign(submits[i].pWaitSemaphores, submits[i].pWaitSemaphores + submits[i].waitSemaphoreCount);
			signals[i].assign(submits[i].pSignalSemaphores, submits[i].pSignalSemaphores + submits[i].signalSemaphoreCount);
		}
		Submitted(device, queue, batches, waits, signals, fence);
		return device->next.vkQueueSubmit(queue, count, submits, fence);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkQueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence) {
		Device* device = DeviceOf(queue);
		std::vector<std::vector<VkCommandBuffer>> batches(count);
		std::vector<std::vector<VkSemaphore>> waits(count), signals(count);
		for (u32 i = 0; i < count; ++i) {
			for (u32 j = 0; j < submits[i].commandBufferInfoCount; ++j) batches[i].push_back(submits[i].pCommandBufferInfos[j].commandBuffer);
			for (u32 j = 0; j < submits[i].waitSemaphoreInfoCount; ++j) waits[i].push_back(submits[i].pWaitSemaphoreInfos[j].semaphore);
			for (u32 j = 0; j < submits[i].signalSemaphoreInfoCount; ++j) signals[i].push_back(submits[i].pSignalSemaphoreInfos[j].semaphore);
		}
		Submitted(device, queue, batches, waits, signals, fence);
		return device->next.vkQueueSubmit2(queue, count, submits, fence);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
		Device* device = DeviceOf(queue);
		std::vector<VkSemaphore> waits(info->pWaitSemaphores, info->pWaitSemaphores + info->waitSemaphoreCount);
		device->frames.BeforePresent(queue, *info, waits);
		VkPresentInfoKHR present = *info;
		present.waitSemaphoreCount = (u32)waits.size();
		present.pWaitSemaphores = waits.data();
		const VkResult result = device->next.vkQueuePresentKHR(queue, &present);
		device->frames.AfterPresent(queue);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkQueueBeginDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* label) {
		Device* device = DeviceOf(queue);
		if (device->next.vkQueueBeginDebugUtilsLabelEXT) device->next.vkQueueBeginDebugUtilsLabelEXT(queue, label);
		device->frames.QueueLabel(queue, LabelOp::Begin, label);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkQueueEndDebugUtilsLabelEXT(VkQueue queue) {
		Device* device = DeviceOf(queue);
		if (device->next.vkQueueEndDebugUtilsLabelEXT) device->next.vkQueueEndDebugUtilsLabelEXT(queue);
		device->frames.QueueLabel(queue, LabelOp::End, nullptr);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkQueueInsertDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* label) {
		Device* device = DeviceOf(queue);
		if (device->next.vkQueueInsertDebugUtilsLabelEXT) device->next.vkQueueInsertDebugUtilsLabelEXT(queue, label);
		device->frames.QueueLabel(queue, LabelOp::Insert, label);
	}

	static PFN_vkVoidFunction CommandHook(const char* name) {
		struct Entry
		{
			const char* name;
			PFN_vkVoidFunction function;
		};
#define FB_HOOK(name) {#name, (PFN_vkVoidFunction)&hook_##name},
		static const Entry entries[] = {
			FB_HOOK(vkCmdBindPipeline) FB_HOOK(vkCmdSetViewport) FB_HOOK(vkCmdSetScissor) FB_HOOK(vkCmdSetViewportWithCount) FB_HOOK(vkCmdSetScissorWithCount)
			FB_HOOK(vkCmdSetLineWidth) FB_HOOK(vkCmdSetDepthBias) FB_HOOK(vkCmdSetBlendConstants) FB_HOOK(vkCmdSetDepthBounds) FB_HOOK(vkCmdSetStencilCompareMask)
			FB_HOOK(vkCmdSetStencilWriteMask) FB_HOOK(vkCmdSetStencilReference) FB_HOOK(vkCmdSetCullMode) FB_HOOK(vkCmdSetFrontFace)
			FB_HOOK(vkCmdSetPrimitiveTopology) FB_HOOK(vkCmdSetDepthTestEnable) FB_HOOK(vkCmdSetDepthWriteEnable) FB_HOOK(vkCmdSetDepthCompareOp)
			FB_HOOK(vkCmdSetDepthBoundsTestEnable) FB_HOOK(vkCmdSetStencilTestEnable) FB_HOOK(vkCmdSetStencilOp) FB_HOOK(vkCmdSetRasterizerDiscardEnable)
			FB_HOOK(vkCmdSetDepthBiasEnable) FB_HOOK(vkCmdSetPrimitiveRestartEnable) FB_HOOK(vkCmdBindDescriptorSets) FB_HOOK(vkCmdBindIndexBuffer)
			FB_HOOK(vkCmdBindIndexBuffer2KHR) FB_HOOK(vkCmdBindVertexBuffers) FB_HOOK(vkCmdBindVertexBuffers2) FB_HOOK(vkCmdPushConstants) FB_HOOK(vkCmdDraw)
			FB_HOOK(vkCmdDrawIndexed) FB_HOOK(vkCmdDrawIndirect) FB_HOOK(vkCmdDrawIndexedIndirect) FB_HOOK(vkCmdDrawIndirectCount)
			FB_HOOK(vkCmdDrawIndexedIndirectCount) FB_HOOK(vkCmdDispatch) FB_HOOK(vkCmdDispatchBase) FB_HOOK(vkCmdDispatchIndirect) FB_HOOK(vkCmdCopyBuffer)
			FB_HOOK(vkCmdCopyImage) FB_HOOK(vkCmdBlitImage) FB_HOOK(vkCmdCopyBufferToImage) FB_HOOK(vkCmdCopyImageToBuffer) FB_HOOK(vkCmdUpdateBuffer)
			FB_HOOK(vkCmdFillBuffer) FB_HOOK(vkCmdClearColorImage) FB_HOOK(vkCmdClearDepthStencilImage) FB_HOOK(vkCmdClearAttachments) FB_HOOK(vkCmdResolveImage)
			FB_HOOK(vkCmdCopyBuffer2) FB_HOOK(vkCmdCopyImage2) FB_HOOK(vkCmdBlitImage2) FB_HOOK(vkCmdCopyBufferToImage2) FB_HOOK(vkCmdCopyImageToBuffer2)
			FB_HOOK(vkCmdResolveImage2) FB_HOOK(vkCmdPipelineBarrier) FB_HOOK(vkCmdPipelineBarrier2) FB_HOOK(vkCmdWaitEvents) FB_HOOK(vkCmdWaitEvents2)
			FB_HOOK(vkCmdSetEvent) FB_HOOK(vkCmdResetEvent) FB_HOOK(vkCmdSetEvent2) FB_HOOK(vkCmdResetEvent2) FB_HOOK(vkCmdBeginRenderPass)
			FB_HOOK(vkCmdBeginRenderPass2) FB_HOOK(vkCmdNextSubpass) FB_HOOK(vkCmdNextSubpass2) FB_HOOK(vkCmdEndRenderPass) FB_HOOK(vkCmdEndRenderPass2)
			FB_HOOK(vkCmdBeginRendering) FB_HOOK(vkCmdEndRendering) FB_HOOK(vkCmdBeginDebugUtilsLabelEXT) FB_HOOK(vkCmdEndDebugUtilsLabelEXT)
			FB_HOOK(vkCmdInsertDebugUtilsLabelEXT) FB_HOOK(vkCmdResetQueryPool) FB_HOOK(vkCmdBeginQuery) FB_HOOK(vkCmdEndQuery) FB_HOOK(vkCmdWriteTimestamp)
			FB_HOOK(vkCmdWriteTimestamp2) FB_HOOK(vkCmdCopyQueryPoolResults) FB_HOOK(vkCmdExecuteCommands) FB_HOOK(vkCmdPushDescriptorSetKHR)
			FB_HOOK(vkCmdDrawMeshTasksEXT) FB_HOOK(vkCmdTraceRaysKHR) FB_HOOK(vkQueueSubmit) FB_HOOK(vkQueueSubmit2) FB_HOOK(vkQueuePresentKHR)
			FB_HOOK(vkQueueBeginDebugUtilsLabelEXT) FB_HOOK(vkQueueEndDebugUtilsLabelEXT) FB_HOOK(vkQueueInsertDebugUtilsLabelEXT)
		};
#undef FB_HOOK
		for (const Entry& entry : entries)
			if (!strcmp(entry.name, name)) return entry.function;
		return nullptr;
	}

	PFN_vkVoidFunction HookFor(const char* name) {
		PFN_vkVoidFunction hook = ObjectHook(name);
		return hook ? hook : CommandHook(name);
	}
}
