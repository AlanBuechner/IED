// The state an event sees, without the GPU: its command buffer's binds and sets before it (Vulkan state does not cross
// command buffers), and the descriptor sets' contents from the frame's start and the updates before its submission.
#include "vkreplay.h"

namespace fb::replay::vulkan
{
	Snapshot Simulate(const Model& model, u32 eid) {
		Snapshot s;
		s.event = model.EventAt(eid);
		if (!s.event || s.event->buffer < 0) return s;
		const FrameOp& op = model.frame[s.event->op];
		const BufferRef& buffer = op.buffers[(size_t)s.event->buffer];
		ModelMap map(model);
		Arena arena;
		std::vector<u8> storage;
		for (i32 c = 0; c <= s.event->command && (size_t)c < buffer.commands.size(); ++c) {
			const CommandRef& command = buffer.commands[(size_t)c];
			storage.assign(CommandArgsSize(command.command) + 16, 0);
			void* raw = storage.data();
			if (!model.DecodeCommandIn(arena, command, raw, &map)) continue;
			if (c == s.event->command) s.command = command.command;
			switch (command.command) {
				case Cmd::BindPipeline: {
					auto& a = *(BindPipelineArgs*)raw;
					(a.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ? s.computePipeline : s.graphicsPipeline) = IdOf(a.pipeline);
					break;
				}
				case Cmd::BindDescriptorSets: {
					auto& a = *(BindDescriptorSetsArgs*)raw;
					const bool compute = a.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;
					(compute ? s.computeLayout : s.graphicsLayout) = IdOf(a.layout);
					for (u32 i = 0; i < a.setCount; ++i) (compute ? s.computeSets : s.graphicsSets)[a.firstSet + i] = IdOf(a.sets[i]);
					break;
				}
				case Cmd::BindIndexBuffer: {
					auto& a = *(BindIndexBufferArgs*)raw;
					s.indexBuffer = IdOf(a.buffer);
					s.indexOffset = a.offset;
					s.indexType = a.indexType;
					break;
				}
				case Cmd::BindVertexBuffers: {
					auto& a = *(BindVertexBuffersArgs*)raw;
					for (u32 i = 0; i < a.count; ++i) s.vertexBuffers[a.first + i] = {IdOf(a.buffers[i]), a.offsets ? a.offsets[i] : 0, a.strides ? a.strides[i] : 0};
					break;
				}
				case Cmd::SetViewport: {
					auto& a = *(ViewportArgs*)raw;
					if (s.viewports.size() < a.first + a.count) s.viewports.resize(a.first + a.count);
					for (u32 i = 0; i < a.count; ++i) s.viewports[a.first + i] = a.viewports[i];
					break;
				}
				case Cmd::SetScissor: {
					auto& a = *(ScissorArgs*)raw;
					if (s.scissors.size() < a.first + a.count) s.scissors.resize(a.first + a.count);
					for (u32 i = 0; i < a.count; ++i) s.scissors[a.first + i] = a.scissors[i];
					break;
				}
				case Cmd::SetDynamic: {
					auto& a = *(SetDynamicArgs*)raw;
					std::array<u32, 6> values;
					for (int i = 0; i < 6; ++i) values[i] = a.values[i];
					s.dynamic[(u32)a.state] = values;
					break;
				}
				case Cmd::SetBlendConstants: memcpy(s.blend, ((BlendConstantsArgs*)raw)->constants, sizeof(s.blend)); break;
				case Cmd::SetStencilReference: s.stencilReference = ((StencilArgs*)raw)->value; break;
				case Cmd::PushConstants: {
					auto& a = *(PushConstantsArgs*)raw;
					if (s.pushConstants.size() < a.offset + a.size) s.pushConstants.resize(a.offset + a.size);
					if (a.values) memcpy(s.pushConstants.data() + a.offset, a.values, a.size);
					break;
				}
				case Cmd::BeginRenderPass: {
					auto& a = *(BeginRenderPassArgs*)raw;
					if (!a.begin) break;
					s.inPass = true;
					s.renderPass = IdOf(a.begin->renderPass);
					s.framebuffer = IdOf(a.begin->framebuffer);
					s.subpass = 0;
					s.passViews.clear();
					for (const VkBaseInStructure* next = (const VkBaseInStructure*)a.begin->pNext; next; next = next->pNext)
						if (next->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
							auto* attachments = (const VkRenderPassAttachmentBeginInfo*)next;
							for (u32 i = 0; i < attachments->attachmentCount; ++i) s.passViews.push_back(IdOf(attachments->pAttachments[i]));
						}
					break;
				}
				case Cmd::NextSubpass: ++s.subpass; break;
				case Cmd::EndRenderPass:
					if (c < s.event->command) s.inPass = false;
					break;
				case Cmd::BeginRendering: {
					auto& a = *(BeginRenderingArgs*)raw;
					if (!a.info) break;
					s.rendering = true;
					s.colorViews.clear();
					for (u32 i = 0; i < a.info->colorAttachmentCount; ++i) s.colorViews.push_back(IdOf(a.info->pColorAttachments[i].imageView));
					s.depthView = a.info->pDepthAttachment ? IdOf(a.info->pDepthAttachment->imageView) : 0;
					s.stencilView = a.info->pStencilAttachment ? IdOf(a.info->pStencilAttachment->imageView) : 0;
					break;
				}
				case Cmd::EndRendering:
					if (c < s.event->command) s.rendering = false;
					break;
				default: break;
			}
		}
		return s;
	}

	std::map<u64, std::map<u64, Descriptor>> DescriptorsAt(const Model& model, u32 eid) {
		std::map<u64, std::map<u64, Descriptor>> sets;
		ModelMap map(model);
		Arena arena;
		auto apply = [&](const u8* data, u64 size) {
			DescriptorUpdate update = {};
			if (!model.DecodeIn(arena, data, size, update, &map)) return;
			for (u32 w = 0; w < update.writeCount; ++w) {
				const VkWriteDescriptorSet& write = update.writes[w];
				for (u32 i = 0; i < write.descriptorCount; ++i) {
					Descriptor d = {};
					d.type = write.descriptorType;
					if (write.pImageInfo) d.image = write.pImageInfo[i];
					if (write.pBufferInfo) d.buffer = write.pBufferInfo[i];
					if (write.pTexelBufferView) d.texel = write.pTexelBufferView[i];
					sets[IdOf(write.dstSet)][((u64)write.dstBinding << 32) | (write.dstArrayElement + i)] = d;
				}
			}
			for (u32 c = 0; c < update.copyCount; ++c) {
				const VkCopyDescriptorSet& copy = update.copies[c];
				for (u32 i = 0; i < copy.descriptorCount; ++i) {
					auto& from = sets[IdOf(copy.srcSet)];
					auto found = from.find(((u64)copy.srcBinding << 32) | (copy.srcArrayElement + i));
					if (found != from.end()) sets[IdOf(copy.dstSet)][((u64)copy.dstBinding << 32) | (copy.dstArrayElement + i)] = found->second;
				}
			}
		};
		for (const auto& record : model.setupDescriptors) apply(record.first, record.second);
		for (const FrameOp& op : model.frame) {
			if (op.kind == OpKind::DescriptorUpdate) apply(op.data, op.size);
			if (op.kind != OpKind::Submit || op.buffers.empty()) continue;
			// Updates after the submission that holds the event are not seen by it.
			u32 last = op.eid;
			for (const BufferRef& buffer : op.buffers)
				if (!buffer.commands.empty()) last = std::max(last, buffer.commands.back().eid);
			if (op.eid <= eid && eid <= last) break;
			if (op.eid > eid) break;
		}
		return sets;
	}
}
