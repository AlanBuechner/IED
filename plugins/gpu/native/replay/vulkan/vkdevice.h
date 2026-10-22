// vkdevice.h - the Vulkan replay device (docs/22 "Replay"): an instance and device on the capture's GPU (or the closest),
// every object of the capture made again (memory, buffers and images first, then their bindings, then the rest, in
// creation order), the frame's start restored before each replay (host memory written, buffers and images copied from
// a pristine upload buffer, image layouts set, descriptor sets written), and the frame replayed into the layer's own
// command buffers up to an event - a cut inside a render pass ends the pass - with the images' layouts tracked so a
// read after it knows where each image is.
#pragma once

#include "replay/replay.h"
#include "vkmodel.h"

#include <unordered_map>

namespace fb::replay::vulkan
{
#define FB_VKR_INSTANCE(X) \
	X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceFeatures) \
	X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkCreateDebugUtilsMessengerEXT) \
	X(vkDestroyDebugUtilsMessengerEXT)

#define FB_VKR_DEVICE(X) \
	X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkAllocateMemory) X(vkFreeMemory) \
	X(vkMapMemory) X(vkUnmapMemory) X(vkFlushMappedMemoryRanges) X(vkInvalidateMappedMemoryRanges) X(vkCreateBuffer) X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
	X(vkBindImageMemory) X(vkCreateImageView) X(vkCreateBufferView) X(vkCreateSampler) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
	X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateRenderPass) \
	X(vkCreateRenderPass2) X(vkCreateFramebuffer) X(vkCreateGraphicsPipelines) X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateQueryPool) \
	X(vkDestroyQueryPool) X(vkGetQueryPoolResults) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
	X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) \
	X(vkResetFences) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetViewportWithCount) X(vkCmdSetScissorWithCount) \
	X(vkCmdSetLineWidth) X(vkCmdSetDepthBias) X(vkCmdSetBlendConstants) X(vkCmdSetDepthBounds) X(vkCmdSetStencilCompareMask) \
	X(vkCmdSetStencilWriteMask) X(vkCmdSetStencilReference) X(vkCmdSetCullMode) X(vkCmdSetFrontFace) X(vkCmdSetPrimitiveTopology) \
	X(vkCmdSetDepthTestEnable) X(vkCmdSetDepthWriteEnable) X(vkCmdSetDepthCompareOp) X(vkCmdSetDepthBoundsTestEnable) \
	X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) X(vkCmdSetRasterizerDiscardEnable) X(vkCmdSetDepthBiasEnable) \
	X(vkCmdSetPrimitiveRestartEnable) X(vkCmdBindDescriptorSets) X(vkCmdBindIndexBuffer) X(vkCmdBindIndexBuffer2KHR) \
	X(vkCmdBindVertexBuffers) X(vkCmdBindVertexBuffers2) X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdDrawIndirect) X(vkCmdDrawIndexedIndirect) \
	X(vkCmdDrawIndirectCount) X(vkCmdDrawIndexedIndirectCount) X(vkCmdDispatch) X(vkCmdDispatchBase) X(vkCmdDispatchIndirect) \
	X(vkCmdCopyBuffer) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdUpdateBuffer) \
	X(vkCmdFillBuffer) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) X(vkCmdClearAttachments) X(vkCmdResolveImage) \
	X(vkCmdCopyBuffer2) X(vkCmdCopyImage2) X(vkCmdBlitImage2) X(vkCmdCopyBufferToImage2) X(vkCmdCopyImageToBuffer2) X(vkCmdResolveImage2) \
	X(vkCmdPipelineBarrier) X(vkCmdPipelineBarrier2) X(vkCmdPushConstants) X(vkCmdBeginRenderPass) X(vkCmdNextSubpass) X(vkCmdEndRenderPass) \
	X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBeginDebugUtilsLabelEXT) X(vkCmdEndDebugUtilsLabelEXT) \
	X(vkCmdInsertDebugUtilsLabelEXT) X(vkCmdResetQueryPool) X(vkCmdBeginQuery) X(vkCmdEndQuery) X(vkCmdWriteTimestamp) \
	X(vkCmdWriteTimestamp2) X(vkCmdCopyQueryPoolResults)

	struct Functions
	{
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
		PFN_vkCreateInstance vkCreateInstance = nullptr;
		PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
		PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
#define FB_VKR_DECLARE(name) PFN_##name name = nullptr;
		FB_VKR_INSTANCE(FB_VKR_DECLARE)
		FB_VKR_DEVICE(FB_VKR_DECLARE)
#undef FB_VKR_DECLARE
	};


	struct ImageState
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageCreateInfo info = {};
		std::vector<VkImageLayout> start;     // at the frame's start (layer-major)
		std::vector<VkImageLayout> layouts;   // now
	};

	struct BufferState
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		u64 size = 0;
	};

	struct ViewState
	{
		u64 image = 0;
		VkImageSubresourceRange range = {};
		VkFormat format = VK_FORMAT_UNDEFINED;
	};

	// What a replayed command buffer has open when the replay stops inside it.
	struct PassState
	{
		bool inPass = false;
		bool rendering = false;
		u64 renderPass = 0;
		std::vector<u64> views;             // the pass's attachments (image view ids)
		u32 subpass = 0;
		int labels = 0;
		std::vector<std::pair<VkQueryPool, u32>> queries;
		bool graphicsMissing = false;       // the bound pipeline could not be made here: its draws are skipped
		bool computeMissing = false;
	};

	// What a replay tells its caller as it goes: timing brackets every action with queries, the pixel history runs
	// draws again with other state (the action's command and the pass it is in, for RunCommand).
	class ReplayHooks
	{
	public:
		virtual ~ReplayHooks() = default;
		virtual void Begin(VkCommandBuffer cb) {}
		virtual void BeforeAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) {}
		virtual void AfterAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) {}
	};

	class ReplayDevice : public SerReadMap
	{
	public:
		explicit ReplayDevice(const Model& model) : model_(model) {}
		~ReplayDevice() override;

		void Create(const JsonValue& options);
		// Replays the frame up to and including event `eid` (all of it for UINT32_MAX) from the frame's start.
		void ReplayTo(u32 eid, ReplayHooks* hooks = nullptr);
		// Reads after ReplayTo: one aspect of one subresource of an image (tightly packed), a range of a buffer.
		void ReadImage(u64 id, VkImageAspectFlagBits aspect, u32 mip, u32 layer, std::vector<u8>& out);
		void ReadBuffer(u64 id, u64 offset, u64 size, std::vector<u8>& out);

		// SerReadMap: capture ids to this device's handles.
		void* Object(u64 id) override;
		std::string Name(u64 id) override { return model_.NameOf(id); }
		u64 IdOfHandle(u64 handle) const;

		VkCommandBuffer BeginOwn();
		void SubmitOwn(VkCommandBuffer cb);

		// Runs one command into `cb` (vkrun.cpp), keeping `pass` and the images' layouts current.
		void RunCommand(VkCommandBuffer cb, const CommandRef& command, PassState& pass);
		// Ends what `pass` has open, so the command buffer can end.
		void Close(VkCommandBuffer cb, PassState& pass);

		// The first memory type of `bits` with all of `flags`; UINT32_MAX when none has them.
		u32 MemoryType(u32 bits, VkMemoryPropertyFlags flags) const;
		const ImageState* Image(u64 id) const;
		const BufferState* Buffer(u64 id) const;
		const ViewState* View(u64 id) const;

		Functions vk;
		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;
		u32 family = 0;
		VkPhysicalDeviceProperties properties = {};
		VkPhysicalDeviceMemoryProperties memory = {};
		bool pipelineStatistics = false;
		bool storesInShaders = false;      // vertex and fragment shaders can write storage (the shader debugger's trace)
		std::string deviceName;
		std::string driver;
		std::vector<std::string> warnings;
		struct Message
		{
			u32 eid;
			std::string severity;
			std::string text;
		};
		std::vector<Message> messages;
		u32 currentEid = 0;

	private:
		void MakeInstance(bool debugLayer);
		void MakeDevice();
		void MakeObjects();
		void MakeObject(const ObjectEntry& entry);
		void BindResources();
		void Upload();
		void Reset();
		void TrackBarrierLayouts(const CommandRef& command);
		void EndPassLayouts(const PassState& pass);
		VkDeviceMemory Allocate(const VkMemoryRequirements& needs, VkMemoryPropertyFlags flags);
		void Set(u64 id, u64 handle);

		const Model& model_;
		Arena scratch_;                            // what a replay decodes, reset for each command or record
		std::unordered_map<u64, u64> handles_;     // capture id -> handle here
		std::unordered_map<u64, u64> ids_;         // handle here -> capture id
		std::unordered_map<u64, ImageState> images_;
		std::unordered_map<u64, BufferState> buffers_;
		std::unordered_map<u64, ViewState> views_;
		std::unordered_map<u64, VkDeviceMemory> memories_;
		std::unordered_map<u64, std::vector<VkImageLayout>> passFinal_;   // render pass -> its attachments' final layouts
		std::unordered_map<u64, u32> passSubpasses_;
		std::unordered_map<u64, std::vector<u64>> framebufferViews_;
		std::vector<VkDeviceMemory> own_;          // memory the replay allocated for itself
		std::vector<VkBuffer> ownBuffers_;
		VkBuffer pristine_ = VK_NULL_HANDLE;       // the initial contents of buffers and images, uploaded once
		std::unordered_map<u64, u64> pristineBuffer_;                        // buffer id -> offset in it
		std::unordered_map<u64, std::vector<u64>> pristineImage_;           // image id -> offsets of its ImageData, in order
		VkCommandPool pool_ = VK_NULL_HANDLE;
		VkCommandBuffer cb_ = VK_NULL_HANDLE;
		VkFence fence_ = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
		void* loader_ = nullptr;
	};
}
