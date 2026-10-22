// The Vulkan structures as vk_gen serializes them: a graphics pipeline with a pNext chain and optional states round
// trips; a pNext structure Flashbang does not know is left out and noted; a descriptor write reads only the array its
// type uses; the VkFormat table decodes as the DXGI format with the same layout.
#include "testing.h"

#include "vk_records.h"

using namespace fb;

namespace
{
	class IdWriteMap : public SerWriteMap
	{
	public:
		u64 Id(const void* object) override { return (u64)(uintptr_t)object; }
	};

	template <class T> bool RoundTrip(T& in, T& out, Arena& arena, std::string* description = nullptr) {
		ByteWriter writer;
		IdWriteMap writeMap;
		Ser w(writer, &writeMap);
		Serialize(w, in);
		ByteReader reader(writer.Data(), writer.Size());
		SerReadMap readMap;
		JsonWriter json;
		if (description) json.BeginObject();
		Ser r(reader, arena, &readMap, description ? &json : nullptr);
		Serialize(r, out);
		if (description) {
			json.EndObject();
			*description = json.Text();
		}
		return !r.Failed() && reader.AtEnd();
	}
}

TEST(VulkanPipelineRoundTrip) {
	const VkFormat color = VK_FORMAT_B8G8R8A8_UNORM;
	VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &color;
	VkPipelineShaderStageCreateInfo stages[2] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, (VkShaderModule)(uintptr_t)7, "main"},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, (VkShaderModule)(uintptr_t)8, "fragmentMain"}};
	VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
	const VkSampleMask mask = 0x5;
	multisample.pSampleMask = &mask;
	VkGraphicsPipelineCreateInfo in = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	in.pNext = &rendering;
	in.stageCount = 2;
	in.pStages = stages;
	in.pMultisampleState = &multisample;
	in.layout = (VkPipelineLayout)(uintptr_t)9;
	VkGraphicsPipelineCreateInfo out = {};
	Arena arena;
	std::string description;
	CHECK(RoundTrip(in, out, arena, &description));
	CHECK(out.stageCount == 2 && out.pStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT);
	CHECK(out.pStages[1].module == (VkShaderModule)(uintptr_t)8);
	CHECK(std::string(out.pStages[1].pName) == "fragmentMain");
	CHECK(out.pVertexInputState == nullptr && out.pTessellationState == nullptr);
	CHECK(out.pMultisampleState && out.pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_4_BIT);
	CHECK(out.pMultisampleState->pSampleMask && out.pMultisampleState->pSampleMask[0] == 0x5);
	CHECK(out.layout == (VkPipelineLayout)(uintptr_t)9);
	auto* next = (const VkPipelineRenderingCreateInfo*)out.pNext;
	CHECK(next && next->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
	CHECK(next && next->colorAttachmentCount == 1 && next->pColorAttachmentFormats[0] == VK_FORMAT_B8G8R8A8_UNORM);
	CHECK(description.find("\"rasterizationSamples\":\"4\"") != std::string::npos);
	CHECK(description.find("\"B8G8R8A8_UNORM\"") != std::string::npos);
}

TEST(VulkanUnknownNextIsDropped) {
	// An sType no Flashbang structure has, ahead of one it knows: the first is left out (and noted), the second kept.
	VkImageFormatListCreateInfo formats = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
	const VkFormat list[2] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB};
	formats.viewFormatCount = 2;
	formats.pViewFormats = list;
	VkBaseInStructure unknown = {(VkStructureType)0x7FFF0001, (const VkBaseInStructure*)&formats};
	VkImageCreateInfo in = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	in.pNext = &unknown;
	in.imageType = VK_IMAGE_TYPE_2D;
	in.format = VK_FORMAT_R8G8B8A8_UNORM;
	in.extent = {64, 32, 1};
	in.mipLevels = 3;
	in.arrayLayers = 1;
	in.samples = VK_SAMPLE_COUNT_1_BIT;
	in.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	fb::vk::TakeDroppedNext();
	VkImageCreateInfo out = {};
	Arena arena;
	std::string description;
	CHECK(RoundTrip(in, out, arena, &description));
	const std::vector<u32> dropped = fb::vk::TakeDroppedNext();
	CHECK(dropped.size() == 1 && dropped[0] == 0x7FFF0001);
	auto* next = (const VkImageFormatListCreateInfo*)out.pNext;
	CHECK(next && next->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO && next->viewFormatCount == 2 && next->pViewFormats[1] == VK_FORMAT_R8G8B8A8_SRGB);
	CHECK(out.extent.width == 64 && out.mipLevels == 3);
	CHECK(description.find("SAMPLED|TRANSFER_DST") != std::string::npos || description.find("TRANSFER_DST|SAMPLED") != std::string::npos);
}

TEST(VulkanDescriptorWriteReadsItsArray) {
	// A buffer write whose image array points at nothing valid: only the buffer infos are read.
	VkDescriptorBufferInfo buffer = {(VkBuffer)(uintptr_t)5, 16, 64};
	VkWriteDescriptorSet in = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	in.dstSet = (VkDescriptorSet)(uintptr_t)3;
	in.dstBinding = 2;
	in.descriptorCount = 1;
	in.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	in.pImageInfo = (const VkDescriptorImageInfo*)(uintptr_t)0x10;   // ignored by the API, so it may be anything
	in.pBufferInfo = &buffer;
	VkWriteDescriptorSet out = {};
	Arena arena;
	CHECK(RoundTrip(in, out, arena));
	CHECK(out.pImageInfo == nullptr && out.pTexelBufferView == nullptr);
	CHECK(out.pBufferInfo && out.pBufferInfo->offset == 16 && out.pBufferInfo->range == 64 && out.pBufferInfo->buffer == (VkBuffer)(uintptr_t)5);
}

TEST(VulkanFormatsDecode) {
	CHECK(fb::vk::DxgiOf(VK_FORMAT_B8G8R8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT) == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
	CHECK(fb::vk::DxgiOf(VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_DEPTH_BIT) == DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
	CHECK(fb::vk::DxgiOf(VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_ASPECT_STENCIL_BIT) == DXGI_FORMAT_R8_UINT);
	CHECK(fb::vk::DxgiOf(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_STENCIL_BIT) == DXGI_FORMAT_UNKNOWN);
	CHECK(fb::vk::AspectsOf(VK_FORMAT_D32_SFLOAT_S8_UINT) == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
	const fb::vk::FormatBlock bc7 = fb::vk::BlockOf(VK_FORMAT_BC7_UNORM_BLOCK, VK_IMAGE_ASPECT_COLOR_BIT);
	CHECK(bc7.bytes == 16 && bc7.width == 4 && bc7.height == 4);
	CHECK(fb::vk::BlockOf(VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_IMAGE_ASPECT_COLOR_BIT).bytes == 0);
	// A2B10G10R10 (red in the low bits) reads as DXGI's R10G10B10A2.
	const FormatInfo* packed = fb::vk::FormatOf(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_IMAGE_ASPECT_COLOR_BIT);
	const u32 texel = 1023u | (3u << 30);
	float rgba[4];
	CHECK(packed != nullptr);
	if (packed) {
		DecodeRows(*packed, (const u8*)&texel, 4, 1, 1, rgba);
		CHECK(rgba[0] == 1.0f && rgba[1] == 0.0f && rgba[3] == 1.0f);
	}
}
