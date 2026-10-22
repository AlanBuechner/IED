// vkreplay.h - the Vulkan replay behind the ied-gpu/1 requests (replay.h): the model of the capture, the replay device,
// the state an event sees (simulated from its command buffer's commands), and what requests keep between calls.
#pragma once

#include "vkdevice.h"
#include "texels.h"

#include <array>

namespace fb::replay::vulkan
{
	struct Usage
	{
		u32 first = 0;
		u32 last = 0;
		u32 count = 0;
	};

	struct Readback
	{
		u32 eid = 0;
		u64 id = 0;
		u32 aspect = 0;
		u32 mip = 0;
		u32 layer = 0;
		std::vector<u8> bytes;
		bool valid = false;
	};

	struct VertexBinding
	{
		u64 buffer = 0;
		u64 offset = 0;
		u64 stride = 0;          // 0: the pipeline's
	};

	// The state an event sees: what its command buffer bound and set before it (vksimulate.cpp).
	struct Snapshot
	{
		const Event* event = nullptr;
		Cmd command = Cmd::Count;               // the event's command (Count for markers and presents)
		u64 graphicsPipeline = 0;
		u64 computePipeline = 0;
		u64 graphicsLayout = 0;
		u64 computeLayout = 0;
		std::map<u32, u64> graphicsSets;        // set index -> descriptor set id
		std::map<u32, u64> computeSets;
		u64 indexBuffer = 0;
		u64 indexOffset = 0;
		VkIndexType indexType = VK_INDEX_TYPE_UINT16;
		std::map<u32, VertexBinding> vertexBuffers;
		std::vector<VkViewport> viewports;
		std::vector<VkRect2D> scissors;
		std::map<u32, std::array<u32, 6>> dynamic;   // VkDynamicState -> its values
		bool inPass = false;
		u64 renderPass = 0;
		u64 framebuffer = 0;
		u32 subpass = 0;
		std::vector<u64> passViews;             // an imageless framebuffer's views
		bool rendering = false;
		std::vector<u64> colorViews;            // dynamic rendering's attachments
		u64 depthView = 0;
		u64 stencilView = 0;
		std::vector<u8> pushConstants;
		float blend[4] = {};
		u32 stencilReference = 0;
	};
	Snapshot Simulate(const Model& model, u32 eid);

	struct Descriptor
	{
		VkDescriptorType type;
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
		VkBufferView texel;
	};
	// The descriptor sets' contents an event sees: set id -> (binding << 32 | element) -> what it holds (handles as ids).
	std::map<u64, std::map<u64, Descriptor>> DescriptorsAt(const Model& model, u32 eid);
	// The vertex shader's input names by location, from the pipeline's vertex stage.
	std::map<u32, std::string> VertexInputNames(const Model& model, const VkGraphicsPipelineCreateInfo& pipeline);

	class VulkanReplay final : public ApiReplay
	{
	public:
		void Open(CaptureReader& file, const JsonValue& options, JsonWriter& info) override;
		void Events(JsonWriter& out) override;
		void ApiCalls(u32 eid, JsonWriter& out) override;
		void State(u32 eid, JsonWriter& out) override;
		void Resources(JsonWriter& out) override;
		void Texture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Pixel(const JsonValue& params, JsonWriter& out) override;
		void PixelHistory(const JsonValue& params, JsonWriter& out) override;
		void DebugShader(const JsonValue& params, JsonWriter& out) override;
		void Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Shader(const JsonValue& params, JsonWriter& out) override;
		void Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) override;
		void Messages(JsonWriter& out) override;
		void ShaderSource(const JsonValue& params, JsonWriter& out) override;
		void EditShader(const JsonValue& params, JsonWriter& out) override;
		void RevertShader(const JsonValue& params, JsonWriter& out) override;
		void Edits(JsonWriter& out) override;
		void UseEdits(bool on) override;
		~VulkanReplay() override;

		ReplayDevice& Device();
		const Model& GetModel() const { return model_; }

	private:
		void ComputeUsage();
		const Readback& Read(u32 eid, u64 id, VkImageAspectFlagBits aspect, u32 mip, u32 layer);
		void ReadBuffer(u32 eid, u64 id, u64 offset, u64 size, std::vector<u8>& out);
		void ReplayTo(u32 eid);
		// The texel the pixel request `params` names, after event `eid` (0: at the frame's start); `format` gets its name.
		TexelValue PixelAt(u32 eid, const JsonValue& params, std::string& format);
		// Forgets what the last replay left, after one that changed the frame (the pixel history's clamped draws).
		void Unreplayed() {
			replayed_ = false;
			last_.valid = false;
		}

		// Edit and Replay (vkedit.cpp): each edited shader, by its key, with its module, and each pipeline made with
		// edits. While the edits are on the device holds the edited pipelines and these keep the captured ones.
		struct ShaderEdit
		{
			std::string key, stage, language, entry, profile, source;
			std::vector<u32> words;
			VkShaderModule module = VK_NULL_HANDLE;
			std::vector<u64> pipelines;
		};
		struct EditedPipeline
		{
			u64 captured = 0;
			u64 edited = 0;
		};
		// Makes `pipelines` again with every edit that applies to them (with none, the captured pipeline comes back); the
		// names of those that could not be made.
		std::vector<std::string> Rebuild(const std::vector<u64>& pipelines);
		void Forget(ShaderEdit& edit);

		Model model_;
		std::unique_ptr<ReplayDevice> device_;
		std::vector<ShaderEdit> edits_;
		std::map<u64, EditedPipeline> editedPipelines_;
		bool editsOn_ = true;
		std::map<u64, Usage> usage_;
		Readback last_;
		u32 replayedTo_ = UINT32_MAX;
		bool replayed_ = false;
		std::string deviceProblem_;
	};
}
