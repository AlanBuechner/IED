// vklayer.h - the Vulkan capture layer VK_LAYER_IED_flashbang (docs/22 "Capture"), which the loader brings in when the
// program runs with VK_INSTANCE_LAYERS naming it (IED's Launch for Capture sets that and VK_ADD_LAYER_PATH). It
// intercepts object creation (keeping each object's creation record, as serialized when made), descriptor updates,
// memory mapping and every recorded command; at a frame boundary after a trigger, the device's FrameCapture writes the
// objects, the descriptor sets' contents, the images' layouts and the initial contents of memory, buffers and images,
// then the frame's submissions until the next present. Buffers and images get the transfer usages a capture needs to
// read and restore them.
#pragma once

#include "capfile.h"
#include "capture/capture.h"
#include "vk_records.h"

#include <vulkan/vk_layer.h>

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace fb::capture::vulkan
{
	using namespace fb::vk;

	// Instance functions the layer calls down to.
#define FB_VK_INSTANCE_CALLS(X) \
	X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceProperties2) \
	X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceFormatProperties) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkEnumerateDeviceExtensionProperties)

	// Device functions the layer intercepts (each has a hook_<name> in the layer) ...
#define FB_VK_DEVICE_HOOKS(X) \
	X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkGetDeviceQueue2) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
	X(vkCreateBuffer) X(vkDestroyBuffer) X(vkBindBufferMemory) X(vkBindBufferMemory2) X(vkCreateImage) X(vkDestroyImage) \
	X(vkBindImageMemory) X(vkBindImageMemory2) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateBufferView) X(vkDestroyBufferView) \
	X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) \
	X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets) \
	X(vkUpdateDescriptorSetWithTemplate) X(vkCreateRenderPass) X(vkCreateRenderPass2) X(vkDestroyRenderPass) X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) X(vkCreateGraphicsPipelines) X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkFreeCommandBuffers) X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkCreateSemaphore) X(vkDestroySemaphore) \
	X(vkCreateEvent) X(vkDestroyEvent) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
	X(vkGetSwapchainImagesKHR) X(vkQueueSubmit) X(vkQueueSubmit2) X(vkQueuePresentKHR) X(vkSetDebugUtilsObjectNameEXT) \
	X(vkQueueBeginDebugUtilsLabelEXT) X(vkQueueEndDebugUtilsLabelEXT) X(vkQueueInsertDebugUtilsLabelEXT) \
	X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetLineWidth) X(vkCmdSetDepthBias) X(vkCmdSetBlendConstants) \
	X(vkCmdSetDepthBounds) X(vkCmdSetStencilCompareMask) X(vkCmdSetStencilWriteMask) X(vkCmdSetStencilReference) \
	X(vkCmdBindDescriptorSets) X(vkCmdBindIndexBuffer) X(vkCmdBindIndexBuffer2KHR) X(vkCmdBindVertexBuffers) X(vkCmdBindVertexBuffers2) \
	X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdDrawIndirect) X(vkCmdDrawIndexedIndirect) X(vkCmdDrawIndirectCount) \
	X(vkCmdDrawIndexedIndirectCount) X(vkCmdDispatch) X(vkCmdDispatchBase) X(vkCmdDispatchIndirect) X(vkCmdCopyBuffer) X(vkCmdCopyImage) \
	X(vkCmdBlitImage) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdUpdateBuffer) X(vkCmdFillBuffer) X(vkCmdClearColorImage) \
	X(vkCmdClearDepthStencilImage) X(vkCmdClearAttachments) X(vkCmdResolveImage) X(vkCmdCopyBuffer2) X(vkCmdCopyImage2) X(vkCmdBlitImage2) \
	X(vkCmdCopyBufferToImage2) X(vkCmdCopyImageToBuffer2) X(vkCmdResolveImage2) X(vkCmdPipelineBarrier) X(vkCmdPipelineBarrier2) \
	X(vkCmdPushConstants) X(vkCmdBeginRenderPass) X(vkCmdBeginRenderPass2) X(vkCmdNextSubpass) X(vkCmdNextSubpass2) X(vkCmdEndRenderPass) \
	X(vkCmdEndRenderPass2) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBeginDebugUtilsLabelEXT) X(vkCmdEndDebugUtilsLabelEXT) \
	X(vkCmdInsertDebugUtilsLabelEXT) X(vkCmdResetQueryPool) X(vkCmdBeginQuery) X(vkCmdEndQuery) X(vkCmdWriteTimestamp) \
	X(vkCmdWriteTimestamp2) X(vkCmdCopyQueryPoolResults) X(vkCmdExecuteCommands) X(vkCmdSetCullMode) X(vkCmdSetFrontFace) \
	X(vkCmdSetPrimitiveTopology) X(vkCmdSetDepthTestEnable) X(vkCmdSetDepthWriteEnable) X(vkCmdSetDepthCompareOp) \
	X(vkCmdSetDepthBoundsTestEnable) X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) X(vkCmdSetRasterizerDiscardEnable) \
	X(vkCmdSetDepthBiasEnable) X(vkCmdSetPrimitiveRestartEnable) X(vkCmdSetViewportWithCount) X(vkCmdSetScissorWithCount) \
	X(vkCmdSetEvent) X(vkCmdResetEvent) X(vkCmdWaitEvents) X(vkCmdSetEvent2) X(vkCmdResetEvent2) X(vkCmdWaitEvents2) \
	X(vkCmdPushDescriptorSetKHR) X(vkCmdDrawMeshTasksEXT) X(vkCmdTraceRaysKHR)

	// ... and the ones it only calls (for its own copies and waits).
#define FB_VK_DEVICE_CALLS(X) \
	X(vkGetBufferMemoryRequirements) X(vkGetImageMemoryRequirements) X(vkDeviceWaitIdle) X(vkQueueWaitIdle) X(vkWaitForFences) \
	X(vkResetFences) X(vkInvalidateMappedMemoryRanges)

	struct InstanceDispatch
	{
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define FB_VK_DECLARE(name) PFN_##name name = nullptr;
		FB_VK_INSTANCE_CALLS(FB_VK_DECLARE)
	};

	struct DeviceDispatch
	{
		PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
		FB_VK_DEVICE_HOOKS(FB_VK_DECLARE)
		FB_VK_DEVICE_CALLS(FB_VK_DECLARE)
#undef FB_VK_DECLARE
	};

	struct Instance
	{
		VkInstance handle = VK_NULL_HANDLE;
		InstanceDispatch next;
		u32 apiVersion = 0;
		std::string application;
		std::string engine;
		std::vector<std::string> extensions;
	};

	struct LayoutChange
	{
		u64 image;
		VkImageSubresourceRange range;
		VkImageLayout layout;
	};

	// A command buffer's commands as the program recorded them, kept from vkBeginCommandBuffer on (a buffer recorded
	// once and submitted every frame is captured with the frame it is submitted in).
	struct Recording
	{
		ByteWriter commands;
		u32 count = 0;
		std::vector<LayoutChange> layouts;     // in order, applied to the images when the buffer is submitted
		std::vector<std::string> unsupported;  // commands the capture cannot record
		u64 renderPass = 0;                    // the render pass begun last, and its framebuffer (for layout changes)
		u64 framebuffer = 0;
		std::vector<u64> attachments;          // the image views of an imageless framebuffer's pass
		std::vector<u32> droppedNext;          // pNext structures the capture does not know
		bool valid = false;                    // begun and not reset since
	};

	struct DescriptorEntry
	{
		VkDescriptorType type;
		VkDescriptorImageInfo image;
		VkDescriptorBufferInfo buffer;
		VkBufferView texel;
	};

	// Everything the layer keeps of one object: its creation record and what a capture needs of its state.
	struct Object
	{
		u64 id = 0;
		u32 record = 0;            // the creation record's type (vk_records.h)
		ObjectKind kind = ObjectKind::Unknown;
		u64 handle = 0;
		std::string name;
		ByteWriter create;         // the creation record's payload, serialized when it was made (bindings are added later)
		bool alive = true;
		std::vector<u32> droppedNext;   // sTypes of its creation's pNext structures the capture does not know

		// Memory.
		u32 memoryType = 0;
		u32 memoryFlags = 0;       // VkMemoryPropertyFlags
		u64 size = 0;
		void* mapped = nullptr;    // the program's mapping, at mappedOffset
		u64 mappedOffset = 0;
		u64 mappedSize = 0;

		// Buffers and images: their memory (an object id) and size.
		u64 memory = 0;
		u64 memoryOffset = 0;
		u64 requirementsSize = 0;
		VkBufferCreateInfo bufferInfo = {};
		VkImageCreateInfo imageInfo = {};
		std::vector<VkImageLayout> layouts;   // layer-major
		u64 swapchain = 0;
		u32 swapchainIndex = 0;

		// Image views, framebuffers and render passes (for layout changes at the end of a pass).
		u64 viewImage = 0;
		VkImageSubresourceRange viewRange = {};
		std::vector<u64> attachments;               // a framebuffer's views
		std::vector<VkImageLayout> finalLayouts;    // a render pass's attachments
		u64 setLayout = 0;
		u64 pool = 0;

		// Descriptor sets: what each binding's elements hold.
		std::map<u64, DescriptorEntry> descriptors;   // binding << 32 | element

		// Queues, command buffers, swap chains.
		u32 family = 0;
		u32 index = 0;
		u32 familyFlags = 0;
		std::unique_ptr<Recording> recording;
		VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		std::vector<u64> images;
		VkSwapchainCreateInfoKHR swapchainInfo = {};
	};

	class Device;

	// One device's frame capture: the same steps as D3D12's (capture/d3d12/framecapture.h) for Vulkan.
	class FrameCapture
	{
	public:
		explicit FrameCapture(Device& device) : device_(device) {}
		~FrameCapture();

		bool Active() const { return active_; }

		// Frame boundaries: a present (before it, with the program's wait semaphores; after it), or Gpu::Capture's.
		// BeforePresent may replace the semaphores the present waits for (the thumbnail's copy waits for them instead).
		void BeforePresent(VkQueue queue, const VkPresentInfoKHR& info, std::vector<VkSemaphore>& waits);
		void AfterPresent(VkQueue queue);
		void ManualStart(u64 frame);
		void ManualEnd();

		// While a frame is captured.
		void BeforeSubmit(VkQueue queue, const std::vector<std::vector<VkCommandBuffer>>& batches, const std::vector<std::vector<VkSemaphore>>& waits,
			const std::vector<std::vector<VkSemaphore>>& signals, VkFence fence);
		void QueueLabel(VkQueue queue, LabelOp op, const VkDebugUtilsLabelEXT* label);
		void Created(Object* object);
		void Named(Object* object);
		void Destroyed(Object* object);
		void DescriptorsUpdated(const ByteWriter& record);
		void Warn(const std::string& text);
		// Unsupported commands and unknown pNext structures of a submitted buffer, as warnings.
		void Unsupported(const Recording& recording);

	private:
		bool Begin(VkQueue queue);
		void End(VkQueue queue, u64 swapchain, u32 imageIndex, CaptureSummary& summary);
		bool MakeObjects(VkQueue queue);
		void WriteSetup();
		void WriteInitialContents();
		void DiffMappings();
		void Thumbnail(VkQueue queue, const VkPresentInfoKHR& info, std::vector<VkSemaphore>& waits, CaptureSummary& summary);
		std::string MetaJson(const CaptureSummary& summary);
		VkCommandBuffer BeginOwn();
		bool SubmitOwn(VkCommandBuffer cb, const std::vector<VkSemaphore>& waits, VkSemaphore signal);

		template <class R> void Record(u32 type, R& record);

		Device& device_;           // its lock guards the capture too (one lock: no order between two)
		bool active_ = false;
		u64 frame_ = 0;
		CaptureWriter file_;
		std::wstring base_;
		ByteWriter stream_;
		u64 events_ = 0;
		std::vector<std::string> warnings_;

		VkQueue queue_ = VK_NULL_HANDLE;
		u32 family_ = 0;
		VkCommandPool pool_ = VK_NULL_HANDLE;
		VkCommandBuffer cb_ = VK_NULL_HANDLE;
		VkFence fence_ = VK_NULL_HANDLE;
		VkSemaphore copied_ = VK_NULL_HANDLE;

		struct Shadow
		{
			u64 memory;
			u64 offset;
			std::vector<u8> bytes;
		};
		std::vector<Shadow> shadows_;
	};

	// One device the program made, with the objects it made from it.
	class Device : public SerWriteMap
	{
	public:
		VkDevice handle = VK_NULL_HANDLE;
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		Instance* instance = nullptr;
		DeviceDispatch next;
		PFN_vkSetDeviceLoaderData setLoaderData = nullptr;   // for the layer's own command buffers
		u64 id = 0;
		ByteWriter info;                         // the DeviceInfo record
		VkPhysicalDeviceMemoryProperties memory = {};
		std::vector<VkQueueFamilyProperties> families;
		std::recursive_mutex lock;
		FrameCapture frames{*this};

		// Objects in creation order (destroyed ones leave when no capture is running) and by handle.
		std::vector<std::unique_ptr<Object>> objects;
		std::unordered_map<u64, Object*> byHandle;
		std::unordered_map<u64, Object*> byId;

		Object* Add(u64 handle, u32 record, ObjectKind kind);
		Object* Find(u64 handle);
		template <class H> Object* Find(H handle) { return Find((u64)(uintptr_t)handle); }
		Object* FindId(u64 id);
		void Remove(u64 handle);
		void Compact();

		// SerWriteMap: handles to ids (0 for one the layer does not know).
		u64 Id(const void* object) override;

		template <class R> void Serialize(Object* object, R& record) {
			object->create.Clear();
			Ser s(object->create, this);
			fb::vk::Serialize(s, record);
		}
	};

	Device* DeviceOf(const void* dispatchable);
	Instance* InstanceOf(const void* dispatchable);
	std::vector<Device*> Devices();
	u64 NextId();

	// Gpu::Capture's frame bounds for every Vulkan device.
	void ManualStart(u64 frame);
	void ManualEnd();

	// The image layout changes a recorded barrier or render pass makes, applied when the buffer is submitted.
	void ApplyLayouts(Device& device, Recording& recording);

	PFN_vkVoidFunction HookFor(const char* name);
}
