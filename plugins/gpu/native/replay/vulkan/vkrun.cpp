// One recorded command replayed (its handles mapped to the replay's), and what a cut leaves open closed again; the
// images' layouts follow barriers and the ends of render passes.
#include "vkdevice.h"

namespace fb::replay::vulkan
{
	void ReplayDevice::TrackBarrierLayouts(const CommandRef& command) {
		ModelMap map(model_);
		auto set = [&](u64 imageId, const VkImageSubresourceRange& range, VkImageLayout layout) {
			auto found = images_.find(imageId);
			if (found == images_.end()) return;
			ImageState& image = found->second;
			const u32 mips = image.info.mipLevels, layers = image.info.arrayLayers;
			const u32 mipEnd = range.levelCount == VK_REMAINING_MIP_LEVELS ? mips : std::min(mips, range.baseMipLevel + range.levelCount);
			const u32 layerEnd = range.layerCount == VK_REMAINING_ARRAY_LAYERS ? layers : std::min(layers, range.baseArrayLayer + range.layerCount);
			for (u32 layer = range.baseArrayLayer; layer < layerEnd; ++layer)
				for (u32 mip = range.baseMipLevel; mip < mipEnd; ++mip) image.layouts[(size_t)layer * mips + mip] = layout;
		};
		if (command.command == Cmd::PipelineBarrier) {
			PipelineBarrierArgs a = {};
			if (!model_.DecodeCommandIn(scratch_, command, &a, &map)) return;
			for (u32 i = 0; i < a.imageCount; ++i) set(IdOf(a.images[i].image), a.images[i].subresourceRange, a.images[i].newLayout);
		}
		else if (command.command == Cmd::PipelineBarrier2) {
			PipelineBarrier2Args a = {};
			if (!model_.DecodeCommandIn(scratch_, command, &a, &map) || !a.info) return;
			for (u32 i = 0; i < a.info->imageMemoryBarrierCount; ++i)
				set(IdOf(a.info->pImageMemoryBarriers[i].image), a.info->pImageMemoryBarriers[i].subresourceRange, a.info->pImageMemoryBarriers[i].newLayout);
		}
	}

	void ReplayDevice::EndPassLayouts(const PassState& pass) {
		auto finals = passFinal_.find(pass.renderPass);
		if (finals == passFinal_.end()) return;
		for (size_t i = 0; i < pass.views.size() && i < finals->second.size(); ++i) {
			const ViewState* view = View(pass.views[i]);
			if (!view) continue;
			auto found = images_.find(view->image);
			if (found == images_.end()) continue;
			ImageState& image = found->second;
			const VkImageSubresourceRange& range = view->range;
			const u32 mips = image.info.mipLevels, layers = image.info.arrayLayers;
			const u32 mipEnd = range.levelCount == VK_REMAINING_MIP_LEVELS ? mips : std::min(mips, range.baseMipLevel + range.levelCount);
			const u32 layerEnd = range.layerCount == VK_REMAINING_ARRAY_LAYERS ? layers : std::min(layers, range.baseArrayLayer + range.layerCount);
			for (u32 layer = range.baseArrayLayer; layer < layerEnd; ++layer)
				for (u32 mip = range.baseMipLevel; mip < mipEnd; ++mip) image.layouts[(size_t)layer * mips + mip] = finals->second[i];
		}
	}

	void ReplayDevice::Close(VkCommandBuffer cb, PassState& pass) {
		for (const auto& query : pass.queries) vk.vkCmdEndQuery(cb, query.first, query.second);
		pass.queries.clear();
		if (pass.rendering) {
			vk.vkCmdEndRendering(cb);
			pass.rendering = false;
		}
		if (pass.inPass) {
			auto subpasses = passSubpasses_.find(pass.renderPass);
			const u32 count = subpasses == passSubpasses_.end() ? 1 : subpasses->second;
			for (u32 s = pass.subpass + 1; s < count; ++s) vk.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
			vk.vkCmdEndRenderPass(cb);
			EndPassLayouts(pass);
			pass.inPass = false;
		}
		for (; pass.labels > 0; --pass.labels)
			if (vk.vkCmdEndDebugUtilsLabelEXT) vk.vkCmdEndDebugUtilsLabelEXT(cb);
	}

	void ReplayDevice::RunCommand(VkCommandBuffer cb, const CommandRef& command, PassState& pass) {
		scratch_.Reset();
		std::vector<u8> storage(CommandArgsSize(command.command) + 16);
		void* raw = storage.data();
		if (!model_.DecodeCommandIn(scratch_, command, raw, this)) return;
		switch (command.command) {
			case Cmd::BindPipeline: {
				auto& a = *(BindPipelineArgs*)raw;
				bool& missing = a.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ? pass.computeMissing : pass.graphicsMissing;
				missing = !a.pipeline;
				if (a.pipeline) vk.vkCmdBindPipeline(cb, a.bindPoint, a.pipeline);
				return;
			}
			case Cmd::SetViewport: {
				auto& a = *(ViewportArgs*)raw;
				if (command.flags) vk.vkCmdSetViewportWithCount(cb, a.count, a.viewports);
				else vk.vkCmdSetViewport(cb, a.first, a.count, a.viewports);
				return;
			}
			case Cmd::SetScissor: {
				auto& a = *(ScissorArgs*)raw;
				if (command.flags) vk.vkCmdSetScissorWithCount(cb, a.count, a.scissors);
				else vk.vkCmdSetScissor(cb, a.first, a.count, a.scissors);
				return;
			}
			case Cmd::SetLineWidth: vk.vkCmdSetLineWidth(cb, ((FloatArgs*)raw)->value); return;
			case Cmd::SetDepthBias: {
				auto& a = *(DepthBiasArgs*)raw;
				vk.vkCmdSetDepthBias(cb, a.constant, a.clamp, a.slope);
				return;
			}
			case Cmd::SetBlendConstants: vk.vkCmdSetBlendConstants(cb, ((BlendConstantsArgs*)raw)->constants); return;
			case Cmd::SetDepthBounds: {
				auto& a = *(DepthBoundsArgs*)raw;
				vk.vkCmdSetDepthBounds(cb, a.min, a.max);
				return;
			}
			case Cmd::SetStencilCompareMask: vk.vkCmdSetStencilCompareMask(cb, ((StencilArgs*)raw)->faces, ((StencilArgs*)raw)->value); return;
			case Cmd::SetStencilWriteMask: vk.vkCmdSetStencilWriteMask(cb, ((StencilArgs*)raw)->faces, ((StencilArgs*)raw)->value); return;
			case Cmd::SetStencilReference: vk.vkCmdSetStencilReference(cb, ((StencilArgs*)raw)->faces, ((StencilArgs*)raw)->value); return;
			case Cmd::SetDynamic: {
				auto& a = *(SetDynamicArgs*)raw;
				const u32* v = a.values;
				switch (a.state) {
					case VK_DYNAMIC_STATE_CULL_MODE: vk.vkCmdSetCullMode(cb, v[0]); return;
					case VK_DYNAMIC_STATE_FRONT_FACE: vk.vkCmdSetFrontFace(cb, (VkFrontFace)v[0]); return;
					case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY: vk.vkCmdSetPrimitiveTopology(cb, (VkPrimitiveTopology)v[0]); return;
					case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE: vk.vkCmdSetDepthTestEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE: vk.vkCmdSetDepthWriteEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP: vk.vkCmdSetDepthCompareOp(cb, (VkCompareOp)v[0]); return;
					case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE: vk.vkCmdSetDepthBoundsTestEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE: vk.vkCmdSetStencilTestEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_STENCIL_OP: vk.vkCmdSetStencilOp(cb, v[0], (VkStencilOp)v[1], (VkStencilOp)v[2], (VkStencilOp)v[3], (VkCompareOp)v[4]); return;
					case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE: vk.vkCmdSetRasterizerDiscardEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE: vk.vkCmdSetDepthBiasEnable(cb, v[0]); return;
					case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE: vk.vkCmdSetPrimitiveRestartEnable(cb, v[0]); return;
					default: return;
				}
			}
			case Cmd::BindDescriptorSets: {
				auto& a = *(BindDescriptorSetsArgs*)raw;
				for (u32 i = 0; i < a.setCount; ++i)
					if (!a.sets[i]) return;
				if (a.layout) vk.vkCmdBindDescriptorSets(cb, a.bindPoint, a.layout, a.firstSet, a.setCount, a.sets, a.dynamicOffsetCount, a.dynamicOffsets);
				return;
			}
			case Cmd::BindIndexBuffer: {
				auto& a = *(BindIndexBufferArgs*)raw;
				if (!a.buffer) return;
				if (command.flags && vk.vkCmdBindIndexBuffer2KHR) vk.vkCmdBindIndexBuffer2KHR(cb, a.buffer, a.offset, a.size, a.indexType);
				else vk.vkCmdBindIndexBuffer(cb, a.buffer, a.offset, a.indexType);
				return;
			}
			case Cmd::BindVertexBuffers: {
				auto& a = *(BindVertexBuffersArgs*)raw;
				if (command.flags && vk.vkCmdBindVertexBuffers2) vk.vkCmdBindVertexBuffers2(cb, a.first, a.count, a.buffers, a.offsets, a.sizes, a.strides);
				else vk.vkCmdBindVertexBuffers(cb, a.first, a.count, a.buffers, a.offsets);
				return;
			}
			case Cmd::Draw: {
				if (pass.graphicsMissing) return;
				auto& a = *(DrawArgs*)raw;
				vk.vkCmdDraw(cb, a.vertexCount, a.instanceCount, a.firstVertex, a.firstInstance);
				return;
			}
			case Cmd::DrawIndexed: {
				if (pass.graphicsMissing) return;
				auto& a = *(DrawIndexedArgs*)raw;
				vk.vkCmdDrawIndexed(cb, a.indexCount, a.instanceCount, a.firstIndex, a.vertexOffset, a.firstInstance);
				return;
			}
			case Cmd::DrawIndirect:
			case Cmd::DrawIndexedIndirect: {
				if (pass.graphicsMissing) return;
				auto& a = *(DrawIndirectArgs*)raw;
				if (!a.buffer) return;
				if (command.command == Cmd::DrawIndirect) vk.vkCmdDrawIndirect(cb, a.buffer, a.offset, a.drawCount, a.stride);
				else vk.vkCmdDrawIndexedIndirect(cb, a.buffer, a.offset, a.drawCount, a.stride);
				return;
			}
			case Cmd::DrawIndirectCount:
			case Cmd::DrawIndexedIndirectCount: {
				if (pass.graphicsMissing) return;
				auto& a = *(DrawIndirectCountArgs*)raw;
				if (!a.buffer || !a.countBuffer) return;
				if (command.command == Cmd::DrawIndirectCount) vk.vkCmdDrawIndirectCount(cb, a.buffer, a.offset, a.countBuffer, a.countOffset, a.maxDrawCount, a.stride);
				else vk.vkCmdDrawIndexedIndirectCount(cb, a.buffer, a.offset, a.countBuffer, a.countOffset, a.maxDrawCount, a.stride);
				return;
			}
			case Cmd::Dispatch: {
				if (pass.computeMissing) return;
				auto& a = *(DispatchArgs*)raw;
				vk.vkCmdDispatch(cb, a.x, a.y, a.z);
				return;
			}
			case Cmd::DispatchBase: {
				if (pass.computeMissing) return;
				auto& a = *(DispatchBaseArgs*)raw;
				vk.vkCmdDispatchBase(cb, a.baseX, a.baseY, a.baseZ, a.x, a.y, a.z);
				return;
			}
			case Cmd::DispatchIndirect: {
				if (pass.computeMissing) return;
				auto& a = *(DispatchIndirectArgs*)raw;
				if (a.buffer) vk.vkCmdDispatchIndirect(cb, a.buffer, a.offset);
				return;
			}
			case Cmd::CopyBuffer: {
				auto& a = *(CopyBufferArgs*)raw;
				if (a.src && a.dst) vk.vkCmdCopyBuffer(cb, a.src, a.dst, a.count, a.regions);
				return;
			}
			case Cmd::CopyImage: {
				auto& a = *(CopyImageArgs*)raw;
				if (a.src && a.dst) vk.vkCmdCopyImage(cb, a.src, a.srcLayout, a.dst, a.dstLayout, a.count, a.regions);
				return;
			}
			case Cmd::BlitImage: {
				auto& a = *(BlitImageArgs*)raw;
				if (a.src && a.dst) vk.vkCmdBlitImage(cb, a.src, a.srcLayout, a.dst, a.dstLayout, a.count, a.regions, a.filter);
				return;
			}
			case Cmd::CopyBufferToImage: {
				auto& a = *(CopyBufferToImageArgs*)raw;
				if (a.src && a.dst) vk.vkCmdCopyBufferToImage(cb, a.src, a.dst, a.dstLayout, a.count, a.regions);
				return;
			}
			case Cmd::CopyImageToBuffer: {
				auto& a = *(CopyImageToBufferArgs*)raw;
				if (a.src && a.dst) vk.vkCmdCopyImageToBuffer(cb, a.src, a.srcLayout, a.dst, a.count, a.regions);
				return;
			}
			case Cmd::UpdateBuffer: {
				auto& a = *(UpdateBufferArgs*)raw;
				if (a.dst && a.size) vk.vkCmdUpdateBuffer(cb, a.dst, a.offset, a.size, a.data);
				return;
			}
			case Cmd::FillBuffer: {
				auto& a = *(FillBufferArgs*)raw;
				if (a.dst) vk.vkCmdFillBuffer(cb, a.dst, a.offset, a.size, a.data);
				return;
			}
			case Cmd::ClearColorImage: {
				auto& a = *(ClearColorImageArgs*)raw;
				if (a.image) vk.vkCmdClearColorImage(cb, a.image, a.layout, &a.color, a.count, a.ranges);
				return;
			}
			case Cmd::ClearDepthStencilImage: {
				auto& a = *(ClearDepthStencilImageArgs*)raw;
				if (a.image) vk.vkCmdClearDepthStencilImage(cb, a.image, a.layout, &a.value, a.count, a.ranges);
				return;
			}
			case Cmd::ClearAttachments: {
				auto& a = *(ClearAttachmentsArgs*)raw;
				vk.vkCmdClearAttachments(cb, a.attachmentCount, a.attachments, a.rectCount, a.rects);
				return;
			}
			case Cmd::ResolveImage: {
				auto& a = *(ResolveImageArgs*)raw;
				if (a.src && a.dst) vk.vkCmdResolveImage(cb, a.src, a.srcLayout, a.dst, a.dstLayout, a.count, a.regions);
				return;
			}
			case Cmd::CopyBuffer2:
				if (((CopyBuffer2Args*)raw)->info) vk.vkCmdCopyBuffer2(cb, ((CopyBuffer2Args*)raw)->info);
				return;
			case Cmd::CopyImage2:
				if (((CopyImage2Args*)raw)->info) vk.vkCmdCopyImage2(cb, ((CopyImage2Args*)raw)->info);
				return;
			case Cmd::BlitImage2:
				if (((BlitImage2Args*)raw)->info) vk.vkCmdBlitImage2(cb, ((BlitImage2Args*)raw)->info);
				return;
			case Cmd::CopyBufferToImage2:
				if (((CopyBufferToImage2Args*)raw)->info) vk.vkCmdCopyBufferToImage2(cb, ((CopyBufferToImage2Args*)raw)->info);
				return;
			case Cmd::CopyImageToBuffer2:
				if (((CopyImageToBuffer2Args*)raw)->info) vk.vkCmdCopyImageToBuffer2(cb, ((CopyImageToBuffer2Args*)raw)->info);
				return;
			case Cmd::ResolveImage2:
				if (((ResolveImage2Args*)raw)->info) vk.vkCmdResolveImage2(cb, ((ResolveImage2Args*)raw)->info);
				return;
			case Cmd::PipelineBarrier: {
				auto& a = *(PipelineBarrierArgs*)raw;
				vk.vkCmdPipelineBarrier(cb, a.srcStages, a.dstStages, a.dependencyFlags, a.memoryCount, a.memory, a.bufferCount, a.buffers, a.imageCount, a.images);
				TrackBarrierLayouts(command);
				return;
			}
			case Cmd::PipelineBarrier2: {
				auto& a = *(PipelineBarrier2Args*)raw;
				if (!a.info) return;
				vk.vkCmdPipelineBarrier2(cb, a.info);
				TrackBarrierLayouts(command);
				return;
			}
			case Cmd::PushConstants: {
				auto& a = *(PushConstantsArgs*)raw;
				if (a.layout && a.size) vk.vkCmdPushConstants(cb, a.layout, a.stages, a.offset, a.size, a.values);
				return;
			}
			case Cmd::BeginRenderPass: {
				auto& a = *(BeginRenderPassArgs*)raw;
				if (!a.begin || !a.begin->renderPass || !a.begin->framebuffer) return;
				vk.vkCmdBeginRenderPass(cb, a.begin, VK_SUBPASS_CONTENTS_INLINE);   // secondaries are inlined here
				pass.inPass = true;
				pass.subpass = 0;
				pass.renderPass = IdOfHandle((u64)a.begin->renderPass);
				auto views = framebufferViews_.find(IdOfHandle((u64)a.begin->framebuffer));
				pass.views = views != framebufferViews_.end() ? views->second : std::vector<u64>();
				for (const VkBaseInStructure* next = (const VkBaseInStructure*)a.begin->pNext; next; next = next->pNext)
					if (next->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
						auto* attachments = (const VkRenderPassAttachmentBeginInfo*)next;
						pass.views.clear();
						for (u32 i = 0; i < attachments->attachmentCount; ++i) pass.views.push_back(IdOfHandle((u64)attachments->pAttachments[i]));
					}
				return;
			}
			case Cmd::NextSubpass:
				if (!pass.inPass) return;
				vk.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
				++pass.subpass;
				return;
			case Cmd::EndRenderPass:
				if (!pass.inPass) return;
				vk.vkCmdEndRenderPass(cb);
				EndPassLayouts(pass);
				pass.inPass = false;
				return;
			case Cmd::BeginRendering: {
				auto& a = *(BeginRenderingArgs*)raw;
				if (!a.info) return;
				VkRenderingInfo info = *a.info;
				info.flags &= ~VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
				vk.vkCmdBeginRendering(cb, &info);
				pass.rendering = true;
				return;
			}
			case Cmd::EndRendering:
				if (!pass.rendering) return;
				vk.vkCmdEndRendering(cb);
				pass.rendering = false;
				return;
			case Cmd::BeginLabel:
			case Cmd::InsertLabel: {
				auto& a = *(LabelArgs*)raw;
				VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, a.name ? a.name : "", {a.color[0], a.color[1], a.color[2], a.color[3]}};
				if (command.command == Cmd::BeginLabel) {
					if (vk.vkCmdBeginDebugUtilsLabelEXT) vk.vkCmdBeginDebugUtilsLabelEXT(cb, &label);
					++pass.labels;
				}
				else if (vk.vkCmdInsertDebugUtilsLabelEXT) vk.vkCmdInsertDebugUtilsLabelEXT(cb, &label);
				return;
			}
			case Cmd::EndLabel:
				// A label begun in another submission is not open here.
				if (pass.labels <= 0) return;
				if (vk.vkCmdEndDebugUtilsLabelEXT) vk.vkCmdEndDebugUtilsLabelEXT(cb);
				--pass.labels;
				return;
			case Cmd::ResetQueryPool: {
				auto& a = *(ResetQueryPoolArgs*)raw;
				if (a.pool) vk.vkCmdResetQueryPool(cb, a.pool, a.first, a.count);
				return;
			}
			case Cmd::BeginQuery: {
				auto& a = *(BeginQueryArgs*)raw;
				if (!a.pool) return;
				vk.vkCmdBeginQuery(cb, a.pool, a.query, a.flags);
				pass.queries.push_back({a.pool, a.query});
				return;
			}
			case Cmd::EndQuery: {
				auto& a = *(EndQueryArgs*)raw;
				for (size_t i = 0; i < pass.queries.size(); ++i)
					if (pass.queries[i].first == a.pool && pass.queries[i].second == a.query) {
						vk.vkCmdEndQuery(cb, a.pool, a.query);
						pass.queries.erase(pass.queries.begin() + i);
						return;
					}
				return;
			}
			case Cmd::WriteTimestamp: {
				auto& a = *(WriteTimestampArgs*)raw;
				if (!a.pool) return;
				if (command.flags && vk.vkCmdWriteTimestamp2) vk.vkCmdWriteTimestamp2(cb, a.stage, a.pool, a.query);
				else vk.vkCmdWriteTimestamp(cb, (VkPipelineStageFlagBits)a.stage, a.pool, a.query);
				return;
			}
			case Cmd::CopyQueryPoolResults: {
				auto& a = *(CopyQueryPoolResultsArgs*)raw;
				if (a.pool && a.dst) vk.vkCmdCopyQueryPoolResults(cb, a.pool, a.first, a.count, a.dst, a.offset, a.stride, a.flags);
				return;
			}
			default: return;   // ExecuteCommands markers (the commands follow inline) and what the capture could not record
		}
	}
}
