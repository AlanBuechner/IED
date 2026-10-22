// The Pipeline view's tree for Vulkan (docs/22 "Views"): input assembly, the shader stages, the rasterizer, the
// attachments with depth, stencil and blend, every bound descriptor set with each element resolved to its resource,
// and the push constants. Items naming an image or buffer open it in the resource viewer; an attachment or storage
// image carries its role, which the UI's "the event's output" looks for.
#include "bufferlayout.h"
#include "vkreplay.h"
#include "vkspirv.h"

// The separator the UI shows between a value's parts (a middle dot), written as its UTF-8 bytes.
#define DOT "\xC2\xB7"

namespace fb::replay::vulkan
{
	namespace
	{
		class Tree
		{
		public:
			explicit Tree(JsonWriter& out) : out_(out) {}
			void Group(const char* name, const std::string& summary) {
				out_.BeginObject();
				out_.String("name", name);
				out_.String("summary", summary);
				out_.BeginArray("items");
			}
			void EndGroup() {
				out_.EndArray();
				out_.EndObject();
			}
			void Item(const std::string& label, const std::string& value, u64 resource = 0, const char* shader = nullptr, const char* role = nullptr) {
				out_.BeginObject();
				out_.String("label", label);
				out_.String("value", value);
				if (resource) out_.Uint("resource", resource);
				if (shader) out_.String("shader", shader);
				if (role) out_.String("role", role);
				out_.EndObject();
			}
			// A buffer binding: where in the buffer it starts and the layout its elements read with, for the buffer view.
			void Buffer(const std::string& label, const std::string& value, u64 resource, u64 offset, const std::string& layout) {
				out_.BeginObject();
				out_.String("label", label);
				out_.String("value", value);
				out_.Uint("resource", resource);
				out_.Uint("offset", offset);
				if (!layout.empty()) out_.String("layout", layout);
				out_.EndObject();
			}
			void Parent(const std::string& label, const std::string& value) {
				out_.BeginObject();
				out_.String("label", label);
				out_.String("value", value);
				out_.BeginArray("children");
			}
			void EndParent() {
				out_.EndArray();
				out_.EndObject();
			}

		private:
			JsonWriter& out_;
		};

		std::string Bytes(u64 bytes) {
			if (bytes >= 1024 * 1024) return Format("%.1f MB", bytes / (1024.0 * 1024.0));
			if (bytes >= 1024) return Format("%.1f KB", bytes / 1024.0);
			return Format("%llu B", (unsigned long long)bytes);
		}

		template <class E> const char* Name(E value) {
			const char* text = EnumName(value);
			return text ? text : "?";
		}
	}

	struct ViewInfo
	{
		u64 image = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	static ViewInfo ViewOf(const Model& model, u64 view) {
		Create<VkImageViewCreateInfo> record = {};
		if (!model.DecodeObject(view, record)) return {};
		return {IdOf(record.info.image), record.info.format};
	}

	static std::string ShaderText(const Model& model, const VkPipelineShaderStageCreateInfo& stage) {
		Create<VkShaderModuleCreateInfo> module = {};
		if (!model.DecodeObject(IdOf(stage.module), module)) return "SPIR-V";
		return Format("SPIR-V " DOT " %s " DOT " %s", Bytes(module.info.codeSize).c_str(), stage.pName ? stage.pName : "main");
	}

	static const char* StageCode(VkShaderStageFlagBits stage) {
		switch (stage) {
			case VK_SHADER_STAGE_VERTEX_BIT: return "vs";
			case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "tcs";
			case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "tes";
			case VK_SHADER_STAGE_GEOMETRY_BIT: return "gs";
			case VK_SHADER_STAGE_FRAGMENT_BIT: return "fs";
			case VK_SHADER_STAGE_COMPUTE_BIT: return "cs";
			case VK_SHADER_STAGE_TASK_BIT_EXT: return "task";
			case VK_SHADER_STAGE_MESH_BIT_EXT: return "mesh";
			default: return "fs";
		}
	}

	static const char* StageTitle(VkShaderStageFlagBits stage) {
		switch (stage) {
			case VK_SHADER_STAGE_VERTEX_BIT: return "Vertex Shader";
			case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "Tessellation Control Shader";
			case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "Tessellation Evaluation Shader";
			case VK_SHADER_STAGE_GEOMETRY_BIT: return "Geometry Shader";
			case VK_SHADER_STAGE_FRAGMENT_BIT: return "Fragment Shader";
			case VK_SHADER_STAGE_TASK_BIT_EXT: return "Task Shader";
			case VK_SHADER_STAGE_MESH_BIT_EXT: return "Mesh Shader";
			default: return "Shader";
		}
	}

	static bool DynamicValue(const Snapshot& s, VkDynamicState state, u32& value) {
		auto found = s.dynamic.find((u32)state);
		if (found == s.dynamic.end()) return false;
		value = found->second[0];
		return true;
	}

	// The layout of one vertex binding's elements: its attributes at their offsets.
	static std::string VertexLayout(const VkPipelineVertexInputStateCreateInfo& input, u32 binding, u32 stride, const std::map<u32, std::string>& names) {
		std::vector<LayoutField> fields;
		for (u32 a = 0; a < input.vertexAttributeDescriptionCount; ++a) {
			const VkVertexInputAttributeDescription& attribute = input.pVertexAttributeDescriptions[a];
			if (attribute.binding != binding) continue;
			const FormatInfo* info = fb::vk::FormatOf(attribute.format, VK_IMAGE_ASPECT_COLOR_BIT);
			auto name = names.find(attribute.location);
			const std::string field = FieldOf(info, name != names.end() ? name->second : "location" + std::to_string(attribute.location));
			fields.push_back({attribute.offset, info ? info->bytes : 0u, field});
		}
		return BufferLayout(fields, stride);
	}

	static void InputAssembler(const Model& model, const Snapshot& s, const VkGraphicsPipelineCreateInfo* p, Tree& tree) {
		VkPrimitiveTopology topology = p && p->pInputAssemblyState ? p->pInputAssemblyState->topology : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		u32 value = 0;
		if (DynamicValue(s, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, value)) topology = (VkPrimitiveTopology)value;
		tree.Group("Input Assembler", Name(topology));
		tree.Item("Topology", Name(topology));
		if (s.indexBuffer)
			tree.Buffer("Index buffer", Format("%s +%llu " DOT " %s", model.NameOf(s.indexBuffer).c_str(), (unsigned long long)s.indexOffset, Name(s.indexType)), s.indexBuffer,
				s.indexOffset, s.indexType == VK_INDEX_TYPE_UINT16 ? "uint16 index" : s.indexType == VK_INDEX_TYPE_UINT8_EXT ? "uint8 index" : "uint index");
		const VkPipelineVertexInputStateCreateInfo* input = p ? p->pVertexInputState : nullptr;
		const std::map<u32, std::string> names = p ? VertexInputNames(model, *p) : std::map<u32, std::string>();
		for (u32 b = 0; input && b < input->vertexBindingDescriptionCount; ++b) {
			const VkVertexInputBindingDescription& binding = input->pVertexBindingDescriptions[b];
			auto bound = s.vertexBuffers.find(binding.binding);
			if (bound == s.vertexBuffers.end()) {
				tree.Item(Format("VB %u", binding.binding), "(not bound)");
				continue;
			}
			const u64 stride = bound->second.stride ? bound->second.stride : binding.stride;
			tree.Buffer(Format("VB %u", binding.binding), Format("%s +%llu " DOT " %llu B stride%s", model.NameOf(bound->second.buffer).c_str(),
				(unsigned long long)bound->second.offset, (unsigned long long)stride, binding.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE ? " " DOT " per instance" : ""),
				bound->second.buffer, bound->second.offset, VertexLayout(*input, binding.binding, (u32)stride, names));
		}
		if (input && input->vertexAttributeDescriptionCount) {
			tree.Parent("Vertex input", Format("%u attributes", input->vertexAttributeDescriptionCount));
			for (u32 a = 0; a < input->vertexAttributeDescriptionCount; ++a) {
				const VkVertexInputAttributeDescription& attribute = input->pVertexAttributeDescriptions[a];
				auto name = names.find(attribute.location);
				tree.Item(name != names.end() ? Format("%s (%u)", name->second.c_str(), attribute.location) : Format("location %u", attribute.location),
					Format("%s " DOT " binding %u " DOT " +%u", Name(attribute.format), attribute.binding, attribute.offset));
			}
			tree.EndParent();
		}
		tree.EndGroup();
	}

	static void Rasterizer(const Snapshot& s, const VkGraphicsPipelineCreateInfo& p, Tree& tree) {
		VkPipelineRasterizationStateCreateInfo r = p.pRasterizationState ? *p.pRasterizationState : VkPipelineRasterizationStateCreateInfo{};
		u32 value = 0;
		if (DynamicValue(s, VK_DYNAMIC_STATE_CULL_MODE, value)) r.cullMode = value;
		if (DynamicValue(s, VK_DYNAMIC_STATE_FRONT_FACE, value)) r.frontFace = (VkFrontFace)value;
		if (DynamicValue(s, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE, value)) r.depthBiasEnable = value;
		if (DynamicValue(s, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE, value)) r.rasterizerDiscardEnable = value;
		const std::string cull = r.cullMode ? fb::vk::Flags_VkCullModeFlagBits(r.cullMode) : "NONE";
		tree.Group("Rasterizer", Format("%s, cull %s", r.polygonMode == VK_POLYGON_MODE_FILL ? "solid" : Name(r.polygonMode), cull.c_str()));
		tree.Item("Polygon mode", Name(r.polygonMode));
		tree.Item("Cull mode", cull);
		tree.Item("Front face", Name(r.frontFace));
		tree.Item("Depth bias", r.depthBiasEnable ? Format("%g (clamp %g, slope %g)", r.depthBiasConstantFactor, r.depthBiasClamp, r.depthBiasSlopeFactor) : "off");
		tree.Item("Depth clamp", r.depthClampEnable ? "on" : "off");
		tree.Item("Rasterizer discard", r.rasterizerDiscardEnable ? "on" : "off");
		std::vector<VkViewport> viewports = s.viewports;
		std::vector<VkRect2D> scissors = s.scissors;
		if (viewports.empty() && p.pViewportState && p.pViewportState->pViewports) viewports.assign(p.pViewportState->pViewports, p.pViewportState->pViewports + p.pViewportState->viewportCount);
		if (scissors.empty() && p.pViewportState && p.pViewportState->pScissors) scissors.assign(p.pViewportState->pScissors, p.pViewportState->pScissors + p.pViewportState->scissorCount);
		for (size_t i = 0; i < viewports.size(); ++i) {
			const VkViewport& v = viewports[i];
			tree.Item(Format("Viewport %zu", i), Format("%g, %g  %g x %g  depth %g..%g", v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth));
		}
		for (size_t i = 0; i < scissors.size(); ++i) {
			const VkRect2D& rect = scissors[i];
			tree.Item(Format("Scissor %zu", i), Format("%d, %d  %u x %u", rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height));
		}
		tree.EndGroup();
	}

	// The color and depth attachments the event draws into: the render pass's current subpass, or dynamic rendering's.
	static void Attachments(const Model& model, const Snapshot& s, std::vector<u64>& colors, u64& depth) {
		if (s.rendering) {
			colors = s.colorViews;
			depth = s.depthView ? s.depthView : s.stencilView;
			return;
		}
		if (!s.inPass) return;
		RenderPassCreate pass = {};
		if (!model.DecodeObject(s.renderPass, pass)) return;
		std::vector<u64> views = s.passViews;
		if (views.empty()) {
			Create<VkFramebufferCreateInfo> framebuffer = {};
			if (model.DecodeObject(s.framebuffer, framebuffer) && framebuffer.info.pAttachments)
				for (u32 i = 0; i < framebuffer.info.attachmentCount; ++i) views.push_back(IdOf(framebuffer.info.pAttachments[i]));
		}
		auto viewAt = [&](u32 attachment) { return attachment != VK_ATTACHMENT_UNUSED && attachment < views.size() ? views[attachment] : 0; };
		if (pass.version == 2) {
			if (s.subpass >= pass.info2.subpassCount) return;
			const VkSubpassDescription2& sub = pass.info2.pSubpasses[s.subpass];
			for (u32 i = 0; i < sub.colorAttachmentCount; ++i) colors.push_back(viewAt(sub.pColorAttachments[i].attachment));
			if (sub.pDepthStencilAttachment) depth = viewAt(sub.pDepthStencilAttachment->attachment);
		}
		else {
			if (s.subpass >= pass.info.subpassCount) return;
			const VkSubpassDescription& sub = pass.info.pSubpasses[s.subpass];
			for (u32 i = 0; i < sub.colorAttachmentCount; ++i) colors.push_back(viewAt(sub.pColorAttachments[i].attachment));
			if (sub.pDepthStencilAttachment) depth = viewAt(sub.pDepthStencilAttachment->attachment);
		}
	}

	static void OutputMerger(const Model& model, const Snapshot& s, const VkGraphicsPipelineCreateInfo* p, Tree& tree) {
		std::vector<u64> colors;
		u64 depth = 0;
		Attachments(model, s, colors, depth);
		tree.Group("Output Merger", Format("%zu target(s)%s", colors.size(), depth ? " + depth" : ""));
		if (s.rendering) tree.Item("Rendering", "dynamic rendering");
		else if (s.inPass) tree.Item("Render pass", Format("%s " DOT " subpass %u " DOT " %s", model.NameOf(s.renderPass).c_str(), s.subpass, model.NameOf(s.framebuffer).c_str()));
		for (size_t i = 0; i < colors.size(); ++i) {
			const ViewInfo view = ViewOf(model, colors[i]);
			if (!view.image) tree.Item(Format("Color %zu", i), "(unused)");
			else tree.Item(Format("Color %zu", i), Format("%s " DOT " %s", model.NameOf(view.image).c_str(), Name(view.format)), view.image, nullptr, "rt");
		}
		if (depth) {
			const ViewInfo view = ViewOf(model, depth);
			if (view.image) tree.Item("Depth", Format("%s " DOT " %s", model.NameOf(view.image).c_str(), Name(view.format)), view.image, nullptr, "depth");
		}
		if (p && p->pDepthStencilState) {
			VkPipelineDepthStencilStateCreateInfo d = *p->pDepthStencilState;
			u32 value = 0;
			if (DynamicValue(s, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, value)) d.depthTestEnable = value;
			if (DynamicValue(s, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, value)) d.depthWriteEnable = value;
			if (DynamicValue(s, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, value)) d.depthCompareOp = (VkCompareOp)value;
			if (DynamicValue(s, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, value)) d.stencilTestEnable = value;
			tree.Item("Depth test", d.depthTestEnable ? Format("%s, write %s", Name(d.depthCompareOp), d.depthWriteEnable ? "on" : "off") : "off");
			tree.Item("Stencil test", d.stencilTestEnable ? Format("ref %u, front %s", s.stencilReference, Name(d.front.compareOp)) : "off");
		}
		if (p && p->pColorBlendState) {
			const VkPipelineColorBlendStateCreateInfo& b = *p->pColorBlendState;
			for (u32 i = 0; i < b.attachmentCount && b.pAttachments; ++i) {
				const VkPipelineColorBlendAttachmentState& a = b.pAttachments[i];
				const std::string mask = fb::vk::Flags_VkColorComponentFlagBits(a.colorWriteMask);
				tree.Item(Format("Blend %u", i), a.blendEnable ? Format("%s %s %s, alpha %s %s %s, mask %s", Name(a.srcColorBlendFactor), Name(a.colorBlendOp),
					Name(a.dstColorBlendFactor), Name(a.srcAlphaBlendFactor), Name(a.alphaBlendOp), Name(a.dstAlphaBlendFactor), mask.c_str()) : Format("off, mask %s", mask.c_str()));
			}
			tree.Item("Blend constants", Format("%g, %g, %g, %g", s.blend[0], s.blend[1], s.blend[2], s.blend[3]));
		}
		tree.EndGroup();
	}

	static bool ImageType(VkDescriptorType type) {
		return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
			type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	}

	static void DescriptorSets(const Model& model, u32 eid, const std::map<u32, u64>& bound, Tree& tree) {
		const auto contents = DescriptorsAt(model, eid);
		tree.Group("Descriptor Sets", Format("%zu bound", bound.size()));
		for (const auto& set : bound) {
			tree.Parent(Format("Set %u", set.first), model.NameOf(set.second));
			DescriptorSetCreate record = {};
			Create<VkDescriptorSetLayoutCreateInfo> layout = {};
			const auto written = contents.find(set.second);
			if (model.DecodeObject(set.second, record) && model.DecodeObject(record.layout, layout)) {
				for (u32 b = 0; b < layout.info.bindingCount; ++b) {
					const VkDescriptorSetLayoutBinding& binding = layout.info.pBindings[b];
					const u32 shown = std::min<u32>(binding.descriptorCount, 32);
					for (u32 e = 0; e < shown; ++e) {
						const std::string label = binding.descriptorCount > 1 ? Format("b%u[%u] %s", binding.binding, e, Name(binding.descriptorType))
							: Format("b%u %s", binding.binding, Name(binding.descriptorType));
						const Descriptor* d = nullptr;
						if (written != contents.end()) {
							auto found = written->second.find(((u64)binding.binding << 32) | e);
							if (found != written->second.end()) d = &found->second;
						}
						if (!d) {
							tree.Item(label, "(not written)");
							continue;
						}
						if (ImageType(d->type)) {
							const ViewInfo view = ViewOf(model, IdOf(d->image.imageView));
							std::string text = Format("%s " DOT " %s " DOT " %s", model.NameOf(view.image).c_str(), Name(view.format), Name(d->image.imageLayout));
							if (d->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) text += " " DOT " " + model.NameOf(IdOf(d->image.sampler));
							tree.Item(label, text, view.image, nullptr, d->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? "uav" : nullptr);
						}
						else if (d->type == VK_DESCRIPTOR_TYPE_SAMPLER) tree.Item(label, model.NameOf(IdOf(d->image.sampler)));
						else if (d->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || d->type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) tree.Item(label, model.NameOf(IdOf(d->texel)));
						else {
							const u64 buffer = IdOf(d->buffer.buffer);
							const bool uniform = d->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || d->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
							tree.Buffer(label, Format("%s +%llu " DOT " %s", model.NameOf(buffer).c_str(), (unsigned long long)d->buffer.offset,
								d->buffer.range == VK_WHOLE_SIZE ? "whole" : Bytes(d->buffer.range).c_str()), buffer, d->buffer.offset, uniform ? "float4" : "");
						}
					}
					if (binding.descriptorCount > shown) tree.Item(Format("b%u", binding.binding), Format("... %u more", binding.descriptorCount - shown));
				}
			}
			tree.EndParent();
		}
		tree.EndGroup();
	}

	static void PushConstants(const Snapshot& s, Tree& tree) {
		if (s.pushConstants.empty()) return;
		tree.Group("Push Constants", Bytes(s.pushConstants.size()));
		for (size_t at = 0; at < s.pushConstants.size(); at += 16) {
			std::string text;
			for (size_t i = at; i < at + 16 && i + 4 <= s.pushConstants.size(); i += 4) {
				float value;
				memcpy(&value, s.pushConstants.data() + i, 4);
				if (!text.empty()) text += ", ";
				text += Format("%g", value);
			}
			tree.Item(Format("+%zu", at), text);
		}
		tree.EndGroup();
	}

	void VulkanReplay::State(u32 eid, JsonWriter& out) {
		const Snapshot s = Simulate(model_, eid);
		if (!s.event) Fail("no event %u", eid);
		const bool compute = s.command != Cmd::Count && ClassOf(s.command) == CmdClass::Dispatch;
		const u64 pipeline = compute ? s.computePipeline : s.graphicsPipeline;
		const CommandRef* command = model_.CommandOf(*s.event);
		out.Uint("eid", eid);
		out.String("command", command ? Model::CommandName(*command) : s.event->name);
		out.String("kind", EventKindName(s.event->kind));
		out.String("pipeline", pipeline ? model_.NameOf(pipeline) : "");
		out.Uint("pipelineId", pipeline);
		out.BeginArray("groups");
		Tree tree(out);
		if (compute) {
			Create<VkComputePipelineCreateInfo> record = {};
			const bool have = model_.DecodeObject(pipeline, record);
			tree.Group("Compute Shader", have ? ShaderText(model_, record.info.stage) : "no pipeline");
			if (have) tree.Item("Shader", ShaderText(model_, record.info.stage), 0, "cs");
			tree.EndGroup();
			DescriptorSets(model_, eid, s.computeSets, tree);
		}
		else {
			Create<VkGraphicsPipelineCreateInfo> record = {};
			const bool have = model_.DecodeObject(pipeline, record);
			InputAssembler(model_, s, have ? &record.info : nullptr, tree);
			if (have) {
				for (u32 i = 0; i < record.info.stageCount; ++i) {
					const VkPipelineShaderStageCreateInfo& stage = record.info.pStages[i];
					tree.Group(StageTitle(stage.stage), ShaderText(model_, stage));
					tree.Item("Shader", ShaderText(model_, stage), 0, StageCode(stage.stage));
					tree.EndGroup();
				}
				Rasterizer(s, record.info, tree);
			}
			OutputMerger(model_, s, have ? &record.info : nullptr, tree);
			DescriptorSets(model_, eid, s.graphicsSets, tree);
		}
		PushConstants(s, tree);
		out.EndArray();
	}
}
