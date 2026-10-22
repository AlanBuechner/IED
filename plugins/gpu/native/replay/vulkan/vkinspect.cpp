// Reading resources after an event: an image subresource (one aspect) decoded for the viewer, the exact texel of a
// pixel pick, a buffer range, and a draw's vertex inputs for the mesh viewer (attributes named from the vertex shader).
#include "vkreplay.h"
#include "vkspirv.h"

#include "replay/texels.h"

namespace fb::replay::vulkan
{
	struct ImageRead
	{
		ImageCreate record = {};
		VkImageAspectFlagBits aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		u32 mip = 0;
		u32 layer = 0;
		u32 z = 0;               // a 3D image's slice
		u32 width = 0;
		u32 height = 0;
		size_t rowPitch = 0;
		size_t slicePitch = 0;
		const FormatInfo* info = nullptr;
	};

	// The image, aspect and subresource a request names, and how its read-back bytes are laid out.
	static ImageRead Describe(const Model& model, const JsonValue& params) {
		ImageRead r;
		const u64 id = params["id"].Uint(0);
		const ObjectEntry* entry = model.Find(id);
		if (!entry || entry->kind != ObjectKind::Image) Fail("%s is not an image", model.NameOf(id).c_str());
		if (!model.DecodeModel(entry->record, entry->recordSize, r.record)) Fail("the record of %s is damaged", model.NameOf(id).c_str());
		const VkImageCreateInfo& info = r.record.info;
		const u32 aspects = AspectsOf(info.format);
		const bool stencil = params["plane"].Uint(0) == 1 && (aspects & VK_IMAGE_ASPECT_STENCIL_BIT);
		r.aspect = stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT
			: (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		r.mip = (u32)params["mip"].Uint(0);
		if (r.mip >= info.mipLevels) Fail("the image has %u mips", info.mipLevels);
		const u32 slice = (u32)params["slice"].Uint(0);
		const bool volume = info.imageType == VK_IMAGE_TYPE_3D;
		const u32 depth = std::max(1u, info.extent.depth >> r.mip);
		if (volume) r.z = std::min(slice, depth - 1);
		else {
			if (slice >= info.arrayLayers) Fail("the image has %u layers", info.arrayLayers);
			r.layer = slice;
		}
		r.width = std::max(1u, info.extent.width >> r.mip);
		r.height = std::max(1u, info.extent.height >> r.mip);
		r.info = FormatOf(info.format, r.aspect);
		if (!r.info || r.info->codec == Codec::Unknown) Fail("format %s cannot be shown", EnumName(info.format) ? EnumName(info.format) : "?");
		const FormatBlock block = BlockOf(info.format, r.aspect);
		r.rowPitch = (size_t)((r.width + block.width - 1) / block.width) * block.bytes;
		r.slicePitch = r.rowPitch * ((r.height + block.height - 1) / block.height);
		return r;
	}

	void VulkanReplay::Texture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const u64 id = params["id"].Uint(0);
		const ImageRead r = Describe(model_, params);
		if (r.info->codec == Codec::BC6H || r.info->codec == Codec::BC7) Fail("%s is decoded on the GPU, which lands later", r.info->name);
		const Readback& read = Read(eid, id, r.aspect, r.mip, r.layer);
		const VkImageCreateInfo& info = r.record.info;
		const u32 maxSize = (u32)params["maxSize"].Uint(2048);
		ShowTexels(*r.info, read.bytes.data() + (size_t)r.z * r.slicePitch, r.rowPitch, r.width, r.height, maxSize, out, binary);
		const char* format = EnumName(info.format) ? EnumName(info.format) : "?";
		out.Uint("eid", eid);
		out.Uint("id", id);
		out.String("name", model_.NameOf(id));
		out.Uint("mips", info.mipLevels);
		out.Uint("slices", info.imageType == VK_IMAGE_TYPE_3D ? std::max(1u, info.extent.depth >> r.mip) : info.arrayLayers);
		out.String("format", format);
		out.String("view", r.aspect == VK_IMAGE_ASPECT_STENCIL_BIT ? "stencil" : r.aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? "depth" : format);
		out.Bool("srgb", r.info->component == Component::Srgb);
		// Linear values (float and sRGB formats, decoded) are encoded for display; UNORM ones are shown as stored.
		out.Bool("linear", r.info->component == Component::Float || r.info->component == Component::Srgb);
		out.Bool("depth", r.info->depth);
		out.Uint("channels", r.info->channels);
	}

	void VulkanReplay::Pixel(const JsonValue& params, JsonWriter& out) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const u64 id = params["id"].Uint(0);
		const ImageRead r = Describe(model_, params);
		const u32 x = (u32)params["x"].Uint(0), y = (u32)params["y"].Uint(0);
		if (x >= r.width || y >= r.height) Fail("(%u, %u) is outside the %u x %u image", x, y, r.width, r.height);
		const Readback& read = Read(eid, id, r.aspect, r.mip, r.layer);
		out.Uint("x", x);
		out.Uint("y", y);
		out.String("format", EnumName(r.record.info.format) ? EnumName(r.record.info.format) : r.info->name);
		PixelValues(*r.info, read.bytes.data() + (size_t)r.z * r.slicePitch, r.rowPitch, x, y, out);
	}

	void VulkanReplay::Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const u64 id = params["id"].Uint(0);
		const u64 offset = params["offset"].Uint(0);
		const u64 size = std::min<u64>(params["size"].Uint(65536), 16ull * 1024 * 1024);
		ReadBuffer(eid, id, offset, size, binary);
		out.Uint("eid", eid);
		out.Uint("id", id);
		out.Uint("offset", offset);
		out.Uint("size", binary.size());
	}

	// The vertex shader's input names by location, from the pipeline's vertex stage.
	static std::map<u32, std::string> InputNames(const Model& model, const VkGraphicsPipelineCreateInfo& pipeline) {
		std::map<u32, std::string> names;
		for (u32 i = 0; i < pipeline.stageCount; ++i) {
			if (pipeline.pStages[i].stage != VK_SHADER_STAGE_VERTEX_BIT) continue;
			Create<VkShaderModuleCreateInfo> module = {};
			if (!model.DecodeObject(IdOf(pipeline.pStages[i].module), module)) continue;
			SpirvModule spirv;
			if (!ParseSpirv(module.info.pCode, module.info.codeSize / 4, spirv)) continue;
			for (const SpirvInterface& input : spirv.inputs)
				if (input.location >= 0) names[(u32)input.location] = input.name;
		}
		return names;
	}

	void VulkanReplay::Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const Snapshot s = Simulate(model_, eid);
		if (!s.event || (s.command != Cmd::Draw && s.command != Cmd::DrawIndexed)) Fail("event %u is not a draw with vertex input", eid);
		const CommandRef* command = model_.CommandOf(*s.event);
		ModelMap map(model_);
		DrawIndexedArgs indexedArgs = {};
		DrawArgs drawArgs = {};
		if (s.command == Cmd::DrawIndexed) model_.DecodeCommand(*command, &indexedArgs, &map);
		else model_.DecodeCommand(*command, &drawArgs, &map);
		Create<VkGraphicsPipelineCreateInfo> pipeline = {};
		if (!model_.DecodeObject(s.graphicsPipeline, pipeline)) Fail("the draw has no graphics pipeline");
		const VkPipelineVertexInputStateCreateInfo* input = pipeline.info.pVertexInputState;
		VkPrimitiveTopology topology = pipeline.info.pInputAssemblyState ? pipeline.info.pInputAssemblyState->topology : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		auto dynamicTopology = s.dynamic.find((u32)VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
		if (dynamicTopology != s.dynamic.end()) topology = (VkPrimitiveTopology)dynamicTopology->second[0];

		std::vector<u32> indices;
		const bool indexed = s.command == Cmd::DrawIndexed;
		const u32 count = std::min<u32>(indexed ? indexedArgs.indexCount : drawArgs.vertexCount, 1u << 20);
		if (indexed) {
			const u32 indexBytes = s.indexType == VK_INDEX_TYPE_UINT32 ? 4 : s.indexType == VK_INDEX_TYPE_UINT8_KHR ? 1 : 2;
			std::vector<u8> raw;
			ReadBuffer(eid, s.indexBuffer, s.indexOffset + (u64)indexedArgs.firstIndex * indexBytes, (u64)count * indexBytes, raw);
			for (size_t i = 0; i + indexBytes <= raw.size(); i += indexBytes) {
				u32 index = 0;
				memcpy(&index, raw.data() + i, indexBytes);
				indices.push_back((u32)((i64)index + indexedArgs.vertexOffset));
			}
		}
		else
			for (u32 i = 0; i < count; ++i) indices.push_back(drawArgs.firstVertex + i);
		u32 low = UINT32_MAX, high = 0;
		for (u32 index : indices) {
			low = std::min(low, index);
			high = std::max(high, index);
		}
		if (indices.empty()) low = high = 0;
		const u32 vertices = std::min<u32>(high - low + 1, 1u << 16);
		const u32 instance = indexed ? indexedArgs.firstInstance : drawArgs.firstInstance;
		const std::map<u32, std::string> names = InputNames(model_, pipeline.info);

		out.Uint("eid", eid);
		out.String("topology", EnumName(topology) ? EnumName(topology) : "?");
		out.Uint("first", low);
		out.Uint("count", vertices);
		out.BeginArray("attributes");
		std::vector<float> data;
		for (u32 a = 0; input && a < input->vertexAttributeDescriptionCount; ++a) {
			const VkVertexInputAttributeDescription& attribute = input->pVertexAttributeDescriptions[a];
			const VkVertexInputBindingDescription* binding = nullptr;
			for (u32 b = 0; b < input->vertexBindingDescriptionCount; ++b)
				if (input->pVertexBindingDescriptions[b].binding == attribute.binding) binding = &input->pVertexBindingDescriptions[b];
			const FormatInfo* info = FormatOf(attribute.format, VK_IMAGE_ASPECT_COLOR_BIT);
			auto bound = s.vertexBuffers.find(attribute.binding);
			const VertexBinding vb = bound != s.vertexBuffers.end() ? bound->second : VertexBinding();
			const u64 stride = vb.stride ? vb.stride : binding ? binding->stride : 0;
			auto name = names.find(attribute.location);
			out.BeginObject();
			out.String("name", name != names.end() ? name->second : "location");
			out.Uint("index", attribute.location);
			out.String("format", EnumName(attribute.format) ? EnumName(attribute.format) : "?");
			out.Uint("components", info ? info->channels : 0);
			out.Uint("slot", attribute.binding);
			out.EndObject();
			std::vector<u8> raw;
			const bool perInstance = binding && binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE;
			if (vb.buffer && info && !IsBlock(info->codec)) {
				const u64 first = perInstance ? instance : low;
				ReadBuffer(eid, vb.buffer, vb.offset + first * stride, (u64)(perInstance ? 1 : vertices) * stride + info->bytes + attribute.offset, raw);
			}
			for (u32 v = 0; v < vertices; ++v) {
				float value[4] = {0, 0, 0, 1};
				const size_t at = (size_t)(perInstance ? 0 : v) * stride + attribute.offset;
				if (info && at + info->bytes <= raw.size()) DecodeRows(*info, raw.data() + at, 0, 1, 1, value);
				data.insert(data.end(), value, value + 4);
			}
		}
		out.EndArray();
		out.BeginArray("indices");
		for (u32 index : indices) out.Uint(index - low);
		out.EndArray();
		binary.resize(data.size() * sizeof(float));
		memcpy(binary.data(), data.data(), binary.size());
	}
}
