#include "vkdevice.h"

#include <windows.h>
#include <algorithm>
#include <cstring>

namespace fb::replay::vulkan
{
	static constexpr u64 kWaitNs = 120ull * 1000 * 1000 * 1000;

	static void Check(VkResult result, const char* what) {
		if (result != VK_SUCCESS) Fail("%s failed (VkResult %d)", what, (int)result);
	}

	static u64 AlignUp(u64 value, u64 alignment) { return (value + alignment - 1) / alignment * alignment; }

	ReplayDevice::~ReplayDevice() {
		if (device) {
			vk.vkDeviceWaitIdle(device);
			for (VkBuffer buffer : ownBuffers_) vk.vkDestroyBuffer(device, buffer, nullptr);
			for (VkDeviceMemory memory : own_) vk.vkFreeMemory(device, memory, nullptr);
			if (fence_) vk.vkDestroyFence(device, fence_, nullptr);
			if (pool_) vk.vkDestroyCommandPool(device, pool_, nullptr);
			vk.vkDestroyDevice(device, nullptr);
		}
		if (messenger_ && vk.vkDestroyDebugUtilsMessengerEXT) vk.vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
		if (instance) vk.vkDestroyInstance(instance, nullptr);
	}

	void* ReplayDevice::Object(u64 id) {
		auto found = handles_.find(id);
		return found == handles_.end() ? nullptr : (void*)(uintptr_t)found->second;
	}

	u64 ReplayDevice::IdOfHandle(u64 handle) const {
		auto found = ids_.find(handle);
		return found == ids_.end() ? 0 : found->second;
	}

	void ReplayDevice::Set(u64 id, u64 handle) {
		if (!handle) return;
		handles_[id] = handle;
		ids_[handle] = id;
	}

	const ImageState* ReplayDevice::Image(u64 id) const {
		auto found = images_.find(id);
		return found == images_.end() ? nullptr : &found->second;
	}

	const BufferState* ReplayDevice::Buffer(u64 id) const {
		auto found = buffers_.find(id);
		return found == buffers_.end() ? nullptr : &found->second;
	}

	const ViewState* ReplayDevice::View(u64 id) const {
		auto found = views_.find(id);
		return found == views_.end() ? nullptr : &found->second;
	}

	// --- Instance and device -----------------------------------------------------------------------------------------

	static VKAPI_ATTR VkBool32 VKAPI_CALL OnMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
		auto* replay = (ReplayDevice*)user;
		const char* level = severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? "warning" : "info";
		if (replay->messages.size() < 5000) replay->messages.push_back({replay->currentEid, level, data && data->pMessage ? data->pMessage : ""});
		return VK_FALSE;
	}

	void ReplayDevice::MakeInstance(bool debugLayer) {
		HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
		if (!loader) Fail("Vulkan is not installed here (no vulkan-1.dll)");
		loader_ = loader;
		vk.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(loader, "vkGetInstanceProcAddr");
		vk.vkCreateInstance = (PFN_vkCreateInstance)vk.vkGetInstanceProcAddr(nullptr, "vkCreateInstance");
		vk.vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vk.vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
		vk.vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vk.vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");
		u32 count = 0;
		vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> available(count);
		vk.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
		std::vector<const char*> extensions;
		bool debugUtils = false;
		for (const VkExtensionProperties& e : available)
			if (!strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) debugUtils = true;
		if (debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		std::vector<const char*> layers;
		if (debugLayer) {
			vk.vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> layerProperties(count);
			vk.vkEnumerateInstanceLayerProperties(&count, layerProperties.data());
			for (const VkLayerProperties& l : layerProperties)
				if (!strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) layers.push_back("VK_LAYER_KHRONOS_validation");
			if (layers.empty()) warnings.push_back("the Vulkan validation layer is not installed (the Vulkan SDK has it), so there are no debug messages");
		}
		VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "flashbang-replay";
		app.apiVersion = std::max<u32>(VK_API_VERSION_1_3, model_.device.instanceApiVersion);
		VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		info.pApplicationInfo = &app;
		info.enabledExtensionCount = (u32)extensions.size();
		info.ppEnabledExtensionNames = extensions.data();
		info.enabledLayerCount = (u32)layers.size();
		info.ppEnabledLayerNames = layers.data();
		Check(vk.vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
#define FB_VKR_LOAD(name) vk.name = (PFN_##name)vk.vkGetInstanceProcAddr(instance, #name);
		FB_VKR_INSTANCE(FB_VKR_LOAD)
#undef FB_VKR_LOAD
		if (!layers.empty() && vk.vkCreateDebugUtilsMessengerEXT) {
			VkDebugUtilsMessengerCreateInfoEXT messenger = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
			messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			messenger.pfnUserCallback = OnMessage;
			messenger.pUserData = this;
			vk.vkCreateDebugUtilsMessengerEXT(instance, &messenger, nullptr, &messenger_);
		}
	}

	void ReplayDevice::MakeDevice() {
		u32 count = 0;
		vk.vkEnumeratePhysicalDevices(instance, &count, nullptr);
		if (!count) Fail("there is no Vulkan device here");
		std::vector<VkPhysicalDevice> devices(count);
		vk.vkEnumeratePhysicalDevices(instance, &count, devices.data());
		physical = devices[0];
		for (VkPhysicalDevice candidate : devices) {
			VkPhysicalDeviceProperties p;
			vk.vkGetPhysicalDeviceProperties(candidate, &p);
			if (p.vendorID == model_.device.vendorId && p.deviceID == model_.device.deviceId) {
				physical = candidate;
				break;
			}
		}
		vk.vkGetPhysicalDeviceProperties(physical, &properties);
		vk.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
		deviceName = std::string(properties.deviceName, strnlen(properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE));
		VkPhysicalDeviceDriverProperties driverProperties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
		VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driverProperties};
		if (vk.vkGetPhysicalDeviceProperties2) vk.vkGetPhysicalDeviceProperties2(physical, &properties2);
		driver = std::string(driverProperties.driverInfo, strnlen(driverProperties.driverInfo, VK_MAX_DRIVER_INFO_SIZE));
		if (properties.vendorID != model_.device.vendorId || properties.deviceID != model_.device.deviceId)
			warnings.push_back("the capture ran on " + model_.device.deviceName + "; replaying on " + deviceName + " (memory types and image layouts may differ)");

		u32 familyCount = 0;
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
		family = UINT32_MAX;
		for (u32 i = 0; i < familyCount && family == UINT32_MAX; ++i)
			if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) family = i;
		if (family == UINT32_MAX) Fail("the Vulkan device has no queue that does graphics and compute");

		vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
		std::vector<VkExtensionProperties> available(count);
		vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());
		std::vector<const char*> extensions;
		const VkDeviceCreateInfo& captured = model_.device.create;
		for (u32 i = 0; i < captured.enabledExtensionCount; ++i) {
			const char* name = captured.ppEnabledExtensionNames[i];
			bool here = false;
			for (const VkExtensionProperties& e : available)
				if (name && !strcmp(e.extensionName, name)) here = true;
			if (here) extensions.push_back(name);
			else if (name) warnings.push_back(std::string("the device extension ") + name + " is not available here; the replay goes on without it");
		}

		VkPhysicalDeviceFeatures supported = {};
		vk.vkGetPhysicalDeviceFeatures(physical, &supported);
		pipelineStatistics = supported.pipelineStatisticsQuery == VK_TRUE;
		VkPhysicalDeviceFeatures features = captured.pEnabledFeatures ? *captured.pEnabledFeatures : VkPhysicalDeviceFeatures{};
		bool inChain = false;
		for (auto* next = (VkBaseOutStructure*)captured.pNext; next; next = next->pNext)
			if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
				((VkPhysicalDeviceFeatures2*)next)->features.pipelineStatisticsQuery = supported.pipelineStatisticsQuery;
				inChain = true;
			}
		features.pipelineStatisticsQuery = supported.pipelineStatisticsQuery;
		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queueInfo.queueFamilyIndex = family;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		VkDeviceCreateInfo info = captured;
		info.queueCreateInfoCount = 1;
		info.pQueueCreateInfos = &queueInfo;
		info.enabledLayerCount = 0;
		info.ppEnabledLayerNames = nullptr;
		info.enabledExtensionCount = (u32)extensions.size();
		info.ppEnabledExtensionNames = extensions.data();
		info.pEnabledFeatures = inChain ? nullptr : &features;
		if (vk.vkCreateDevice(physical, &info, nullptr, &device) != VK_SUCCESS) {
			// The program's feature structures may ask for what this GPU lacks: try with the basic features alone.
			info.pNext = nullptr;
			info.pEnabledFeatures = &features;
			Check(vk.vkCreateDevice(physical, &info, nullptr, &device), "vkCreateDevice");
			warnings.push_back("the device's extra features could not be enabled here; pipelines that need them may not replay");
		}
#define FB_VKR_LOAD(name) vk.name = (PFN_##name)vk.vkGetDeviceProcAddr(device, #name);
		FB_VKR_DEVICE(FB_VKR_LOAD)
#undef FB_VKR_LOAD
		vk.vkGetDeviceQueue(device, family, 0, &queue);
		VkCommandPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = family;
		Check(vk.vkCreateCommandPool(device, &poolInfo, nullptr, &pool_), "vkCreateCommandPool");
		VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = pool_;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		Check(vk.vkAllocateCommandBuffers(device, &allocate, &cb_), "vkAllocateCommandBuffers");
		VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		Check(vk.vkCreateFence(device, &fenceInfo, nullptr, &fence_), "vkCreateFence");
	}

	VkCommandBuffer ReplayDevice::BeginOwn() {
		vk.vkResetCommandBuffer(cb_, 0);
		VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vk.vkBeginCommandBuffer(cb_, &begin);
		return cb_;
	}

	void ReplayDevice::SubmitOwn(VkCommandBuffer cb) {
		Check(vk.vkEndCommandBuffer(cb), "vkEndCommandBuffer");
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cb;
		Check(vk.vkQueueSubmit(queue, 1, &submit, fence_), "vkQueueSubmit");
		const VkResult waited = vk.vkWaitForFences(device, 1, &fence_, VK_TRUE, kWaitNs);
		vk.vkResetFences(device, 1, &fence_);
		if (waited != VK_SUCCESS) Fail("the replay's GPU work did not finish (VkResult %d): the device may be lost", (int)waited);
	}

	u32 ReplayDevice::MemoryType(u32 bits, VkMemoryPropertyFlags flags) const {
		for (u32 i = 0; i < memory.memoryTypeCount; ++i)
			if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
		return UINT32_MAX;
	}

	VkDeviceMemory ReplayDevice::Allocate(const VkMemoryRequirements& needs, VkMemoryPropertyFlags flags) {
		u32 type = MemoryType(needs.memoryTypeBits, flags);
		if (type == UINT32_MAX) type = MemoryType(needs.memoryTypeBits, 0);
		VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		info.allocationSize = needs.size;
		info.memoryTypeIndex = type;
		VkDeviceMemory out = VK_NULL_HANDLE;
		Check(vk.vkAllocateMemory(device, &info, nullptr, &out), "vkAllocateMemory");
		own_.push_back(out);
		return out;
	}

	// --- The capture's objects -------------------------------------------------------------------------------------

	void ReplayDevice::MakeObjects() {
		// Memory, buffers and images first; then their bindings (memory may come after what it backs); then the rest,
		// which only names what came before it.
		for (const auto& entry : model_.objects) {
			const ObjectKind kind = entry.second.kind;
			if (kind == ObjectKind::Memory || kind == ObjectKind::Buffer || kind == ObjectKind::Image || kind == ObjectKind::Queue) MakeObject(entry.second);
		}
		BindResources();
		for (const auto& entry : model_.objects) {
			const ObjectKind kind = entry.second.kind;
			if (kind != ObjectKind::Memory && kind != ObjectKind::Buffer && kind != ObjectKind::Image && kind != ObjectKind::Queue) MakeObject(entry.second);
		}
	}

	void ReplayDevice::MakeObject(const ObjectEntry& entry) {
		const u64 id = entry.id;
		// The handle is read after the call that fills it (by reference: the order arguments are evaluated in is open).
		auto made = [&](VkResult result, const auto& handle, const char* what) {
			if (result == VK_SUCCESS) Set(id, (u64)handle);
			else warnings.push_back(Format("%s %s could not be made here (VkResult %d)", what, model_.NameOf(id).c_str(), (int)result));
		};
		switch (entry.recordType) {
			case kQueue: Set(id, (u64)(uintptr_t)queue); return;
			case kMemory: {
				MemoryCreate record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkMemoryAllocateInfo info = record.info;
				if (info.memoryTypeIndex >= memory.memoryTypeCount || (memory.memoryTypes[info.memoryTypeIndex].propertyFlags & record.propertyFlags) != record.propertyFlags) {
					const u32 type = MemoryType(~0u, record.propertyFlags);
					if (type != UINT32_MAX) info.memoryTypeIndex = type;
				}
				VkDeviceMemory out = VK_NULL_HANDLE;
				const VkResult result = vk.vkAllocateMemory(device, &info, nullptr, &out);
				made(result, out, "memory");
				if (result == VK_SUCCESS) memories_[id] = out;
				return;
			}
			case kBuffer: {
				BufferCreate record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkBuffer out = VK_NULL_HANDLE;
				const VkResult result = vk.vkCreateBuffer(device, &record.info, nullptr, &out);
				made(result, out, "buffer");
				if (result == VK_SUCCESS) buffers_[id] = {out, record.info.size};
				return;
			}
			case kImage: {
				ImageCreate record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkImageCreateInfo info = record.info;
				// A swap chain's image is an ordinary image here, which the replay reads and restores.
				if (record.swapchain) info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
				VkImage out = VK_NULL_HANDLE;
				const VkResult result = vk.vkCreateImage(device, &info, nullptr, &out);
				made(result, out, "image");
				if (result != VK_SUCCESS) return;
				ImageState state;
				state.image = out;
				state.info = info;
				state.info.pNext = nullptr;
				state.info.pQueueFamilyIndices = nullptr;
				auto layouts = model_.layouts.find(id);
				state.start = layouts != model_.layouts.end() ? layouts->second : std::vector<VkImageLayout>();
				state.start.resize((size_t)info.arrayLayers * info.mipLevels, VK_IMAGE_LAYOUT_UNDEFINED);
				state.layouts = state.start;
				images_[id] = std::move(state);
				return;
			}
			case kImageView: {
				fb::vk::Create<VkImageViewCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkImageView out = VK_NULL_HANDLE;
				const VkResult result = record.info.image ? vk.vkCreateImageView(device, &record.info, nullptr, &out) : VK_ERROR_UNKNOWN;
				made(result, out, "image view");
				views_[id] = {IdOfHandle((u64)record.info.image), record.info.subresourceRange, record.info.format};
				return;
			}
			case kBufferView: {
				fb::vk::Create<VkBufferViewCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkBufferView out = VK_NULL_HANDLE;
				made(vk.vkCreateBufferView(device, &record.info, nullptr, &out), out, "buffer view");
				return;
			}
			case kSampler: {
				fb::vk::Create<VkSamplerCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkSampler out = VK_NULL_HANDLE;
				made(vk.vkCreateSampler(device, &record.info, nullptr, &out), out, "sampler");
				return;
			}
			case kShaderModule: {
				fb::vk::Create<VkShaderModuleCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkShaderModule out = VK_NULL_HANDLE;
				made(vk.vkCreateShaderModule(device, &record.info, nullptr, &out), out, "shader module");
				return;
			}
			case kDescriptorSetLayout: {
				fb::vk::Create<VkDescriptorSetLayoutCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkDescriptorSetLayout out = VK_NULL_HANDLE;
				made(vk.vkCreateDescriptorSetLayout(device, &record.info, nullptr, &out), out, "descriptor set layout");
				return;
			}
			case kPipelineLayout: {
				fb::vk::Create<VkPipelineLayoutCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkPipelineLayout out = VK_NULL_HANDLE;
				made(vk.vkCreatePipelineLayout(device, &record.info, nullptr, &out), out, "pipeline layout");
				return;
			}
			case kDescriptorPool: {
				fb::vk::Create<VkDescriptorPoolCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				record.info.flags |= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
				VkDescriptorPool out = VK_NULL_HANDLE;
				made(vk.vkCreateDescriptorPool(device, &record.info, nullptr, &out), out, "descriptor pool");
				return;
			}
			case kDescriptorSet: {
				DescriptorSetCreate record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkDescriptorSetLayout layout = (VkDescriptorSetLayout)Object(record.layout);
				VkDescriptorSetVariableDescriptorCountAllocateInfo variable = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
				variable.descriptorSetCount = 1;
				variable.pDescriptorCounts = &record.variableCount;
				VkDescriptorSetAllocateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
				info.pNext = record.variableCount ? &variable : nullptr;
				info.descriptorPool = (VkDescriptorPool)Object(record.pool);
				info.descriptorSetCount = 1;
				info.pSetLayouts = &layout;
				VkDescriptorSet out = VK_NULL_HANDLE;
				made(info.descriptorPool && layout ? vk.vkAllocateDescriptorSets(device, &info, &out) : VK_ERROR_UNKNOWN, out, "descriptor set");
				return;
			}
			case kRenderPass: {
				RenderPassCreate record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkRenderPass out = VK_NULL_HANDLE;
				std::vector<VkImageLayout>& finals = passFinal_[id];
				if (record.version == 2) {
					made(vk.vkCreateRenderPass2(device, &record.info2, nullptr, &out), out, "render pass");
					for (u32 i = 0; i < record.info2.attachmentCount; ++i) finals.push_back(record.info2.pAttachments[i].finalLayout);
					passSubpasses_[id] = record.info2.subpassCount;
				}
				else {
					made(vk.vkCreateRenderPass(device, &record.info, nullptr, &out), out, "render pass");
					for (u32 i = 0; i < record.info.attachmentCount; ++i) finals.push_back(record.info.pAttachments[i].finalLayout);
					passSubpasses_[id] = record.info.subpassCount;
				}
				return;
			}
			case kFramebuffer: {
				fb::vk::Create<VkFramebufferCreateInfo> model = {};
				if (model_.DecodeModel(entry.record, entry.recordSize, model) && model.info.pAttachments)
					for (u32 i = 0; i < model.info.attachmentCount; ++i) framebufferViews_[id].push_back(IdOf(model.info.pAttachments[i]));
				fb::vk::Create<VkFramebufferCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkFramebuffer out = VK_NULL_HANDLE;
				made(vk.vkCreateFramebuffer(device, &record.info, nullptr, &out), out, "framebuffer");
				return;
			}
			case kGraphicsPipeline: {
				fb::vk::Create<VkGraphicsPipelineCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkPipeline out = VK_NULL_HANDLE;
				made(vk.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &record.info, nullptr, &out), out, "pipeline");
				return;
			}
			case kComputePipeline: {
				fb::vk::Create<VkComputePipelineCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkPipeline out = VK_NULL_HANDLE;
				made(vk.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &record.info, nullptr, &out), out, "pipeline");
				return;
			}
			case kQueryPool: {
				fb::vk::Create<VkQueryPoolCreateInfo> record = {};
				if (!model_.Decode(entry.record, entry.recordSize, record, this)) return;
				VkQueryPool out = VK_NULL_HANDLE;
				made(vk.vkCreateQueryPool(device, &record.info, nullptr, &out), out, "query pool");
				return;
			}
			default: return;   // command pools and buffers, fences, semaphores, events and swap chains: the replay has its own
		}
	}

	void ReplayDevice::BindResources() {
		for (const auto& entry : model_.objects) {
			if (entry.second.kind == ObjectKind::Buffer) {
				BufferCreate record = {};
				auto buffer = buffers_.find(entry.first);
				if (buffer == buffers_.end() || !model_.DecodeModel(entry.second.record, entry.second.recordSize, record) || !record.memory) continue;
				auto memoryHere = memories_.find(record.memory);
				if (memoryHere == memories_.end()) continue;
				Check(vk.vkBindBufferMemory(device, buffer->second.buffer, memoryHere->second, record.memoryOffset), "vkBindBufferMemory");
			}
			else if (entry.second.kind == ObjectKind::Image) {
				ImageCreate record = {};
				auto image = images_.find(entry.first);
				if (image == images_.end() || !model_.DecodeModel(entry.second.record, entry.second.recordSize, record)) continue;
				auto memoryHere = record.memory ? memories_.find(record.memory) : memories_.end();
				if (memoryHere != memories_.end()) {
					Check(vk.vkBindImageMemory(device, image->second.image, memoryHere->second, record.memoryOffset), "vkBindImageMemory");
					continue;
				}
				// A swap chain's image (or one the program never bound): memory of its own.
				VkMemoryRequirements needs = {};
				vk.vkGetImageMemoryRequirements(device, image->second.image, &needs);
				Check(vk.vkBindImageMemory(device, image->second.image, Allocate(needs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 0), "vkBindImageMemory");
			}
		}
	}

	// The initial contents of buffers and images in one host buffer, kept for every replay's reset.
	void ReplayDevice::Upload() {
		u64 total = 0;
		for (const auto& contents : model_.bufferContents) {
			pristineBuffer_[contents.first] = total;
			total += AlignUp(contents.second.second, 16);
		}
		for (const auto& contents : model_.imageContents)
			for (const ImageData& data : contents.second) {
				pristineImage_[contents.first].push_back(total);
				total += AlignUp(data.size, 16);
			}
		if (!total) return;
		VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = total;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		Check(vk.vkCreateBuffer(device, &info, nullptr, &pristine_), "vkCreateBuffer");
		ownBuffers_.push_back(pristine_);
		VkMemoryRequirements needs = {};
		vk.vkGetBufferMemoryRequirements(device, pristine_, &needs);
		VkDeviceMemory memoryHere = Allocate(needs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		Check(vk.vkBindBufferMemory(device, pristine_, memoryHere, 0), "vkBindBufferMemory");
		u8* mapped = nullptr;
		Check(vk.vkMapMemory(device, memoryHere, 0, VK_WHOLE_SIZE, 0, (void**)&mapped), "vkMapMemory");
		for (const auto& contents : model_.bufferContents) memcpy(mapped + pristineBuffer_[contents.first], contents.second.first, (size_t)contents.second.second);
		for (const auto& contents : model_.imageContents)
			for (size_t i = 0; i < contents.second.size(); ++i) memcpy(mapped + pristineImage_[contents.first][i], contents.second[i].data, (size_t)contents.second[i].size);
		vk.vkUnmapMemory(device, memoryHere);
	}

	void ReplayDevice::Create(const JsonValue& options) {
		MakeInstance(options["debugLayer"].Bool(false));
		MakeDevice();
		MakeObjects();
		Upload();
	}

	// --- The frame's start ---------------------------------------------------------------------------------------

	static VkImageMemoryBarrier LayoutBarrier(VkImage image, VkImageAspectFlags aspects, u32 mip, u32 layer, VkImageLayout from, VkImageLayout to) {
		VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {aspects, mip, 1, layer, 1};
		return barrier;
	}

	void ReplayDevice::Reset() {
		// Host-visible memory as it was.
		for (const MemoryData& contents : model_.memoryContents) {
			auto memoryHere = memories_.find(contents.memory);
			if (memoryHere == memories_.end()) continue;
			void* mapped = nullptr;
			if (vk.vkMapMemory(device, memoryHere->second, contents.offset, contents.size, 0, &mapped) != VK_SUCCESS) continue;
			memcpy(mapped, contents.data, (size_t)contents.size);
			VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, memoryHere->second, contents.offset, VK_WHOLE_SIZE};
			vk.vkFlushMappedMemoryRanges(device, 1, &range);
			vk.vkUnmapMemory(device, memoryHere->second);
		}
		// Images and buffers in device memory from the pristine copy, then every image in its starting layouts.
		VkCommandBuffer cb = BeginOwn();
		std::vector<VkImageMemoryBarrier> before, after;
		for (auto& entry : images_) {
			ImageState& image = entry.second;
			const VkImageAspectFlags aspects = AspectsOf(image.info.format);
			auto contents = model_.imageContents.find(entry.first);
			const bool restored = contents != model_.imageContents.end();
			for (u32 layer = 0; layer < image.info.arrayLayers; ++layer)
				for (u32 mip = 0; mip < image.info.mipLevels; ++mip) {
					const VkImageLayout start = image.start[(size_t)layer * image.info.mipLevels + mip];
					if (restored) {
						before.push_back(LayoutBarrier(image.image, aspects, mip, layer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
						if (start != VK_IMAGE_LAYOUT_UNDEFINED && start != VK_IMAGE_LAYOUT_PREINITIALIZED)
							after.push_back(LayoutBarrier(image.image, aspects, mip, layer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, start));
					}
					else if (start != VK_IMAGE_LAYOUT_UNDEFINED && start != VK_IMAGE_LAYOUT_PREINITIALIZED)
						after.push_back(LayoutBarrier(image.image, aspects, mip, layer, VK_IMAGE_LAYOUT_UNDEFINED, start));
				}
			image.layouts = image.start;
		}
		if (!before.empty())
			vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, (u32)before.size(), before.data());
		for (const auto& contents : model_.imageContents) {
			auto image = images_.find(contents.first);
			if (image == images_.end()) continue;
			for (size_t i = 0; i < contents.second.size(); ++i) {
				const ImageData& data = contents.second[i];
				VkBufferImageCopy region = {};
				region.bufferOffset = pristineImage_[contents.first][i];
				region.imageSubresource = {data.aspect, data.mip, data.layer, 1};
				region.imageExtent = data.extent;
				vk.vkCmdCopyBufferToImage(cb, pristine_, image->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
			}
		}
		for (const auto& contents : model_.bufferContents) {
			auto buffer = buffers_.find(contents.first);
			if (buffer == buffers_.end()) continue;
			VkBufferCopy region = {pristineBuffer_[contents.first], 0, std::min<u64>(contents.second.second, buffer->second.size)};
			vk.vkCmdCopyBuffer(cb, pristine_, buffer->second.buffer, 1, &region);
		}
		if (!after.empty())
			vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, (u32)after.size(), after.data());
		VkMemoryBarrier all = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
		vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &all, 0, nullptr, 0, nullptr);
		SubmitOwn(cb);
		// The descriptor sets' contents at the start.
		for (const auto& record : model_.setupDescriptors) {
			DescriptorUpdate update = {};
			scratch_.Reset();
			if (model_.DecodeIn(scratch_, record.first, record.second, update, this)) vk.vkUpdateDescriptorSets(device, update.writeCount, update.writes, update.copyCount, update.copies);
		}
	}

	// --- Replaying -------------------------------------------------------------------------------------------------

	void ReplayDevice::ReplayTo(u32 eid, ReplayHooks* hooks) {
		vk.vkDeviceWaitIdle(device);
		Reset();
		ModelMap modelMap(model_);
		for (const FrameOp& op : model_.frame) {
			if (op.kind == OpKind::MapWrite) {
				MapWrite write = {};
				scratch_.Reset();
				if (!model_.DecodeIn(scratch_, op.data, op.size, write, &modelMap)) continue;
				auto memoryHere = memories_.find(write.memory);
				void* mapped = nullptr;
				if (memoryHere == memories_.end() || vk.vkMapMemory(device, memoryHere->second, write.offset, write.size, 0, &mapped) != VK_SUCCESS) continue;
				memcpy(mapped, write.data, (size_t)write.size);
				VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, memoryHere->second, write.offset, VK_WHOLE_SIZE};
				vk.vkFlushMappedMemoryRanges(device, 1, &range);
				vk.vkUnmapMemory(device, memoryHere->second);
				continue;
			}
			if (op.kind == OpKind::DescriptorUpdate) {
				DescriptorUpdate update = {};
				scratch_.Reset();
				if (model_.DecodeIn(scratch_, op.data, op.size, update, this)) vk.vkUpdateDescriptorSets(device, update.writeCount, update.writes, update.copyCount, update.copies);
				continue;
			}
			if (op.kind != OpKind::Submit) {
				if (op.eid && op.eid >= eid) break;
				continue;
			}
			if (op.buffers.empty() || op.eid > eid) {
				if (op.eid > eid) break;
				continue;
			}
			VkCommandBuffer cb = BeginOwn();
			// Each submission sees what the ones before it wrote (the replay runs them one after another).
			VkMemoryBarrier all = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
			vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &all, 0, nullptr, 0, nullptr);
			if (hooks) hooks->Begin(cb);
			PassState pass;
			bool stop = false;
			for (const BufferRef& buffer : op.buffers) {
				for (const CommandRef& command : buffer.commands) {
					if (command.eid > eid) {
						stop = true;
						break;
					}
					currentEid = command.eid;
					const bool action = IsAction(command.command);
					if (action && hooks) hooks->BeforeAction(cb, command, pass);
					RunCommand(cb, command, pass);
					if (action && hooks) hooks->AfterAction(cb, command, pass);
					if (command.eid == eid) stop = true;
				}
				if (stop) break;
			}
			if (stop) Close(cb, pass);
			SubmitOwn(cb);
			if (stop) break;
		}
		currentEid = 0;
	}

	// --- Reads -------------------------------------------------------------------------------------------------------

	void ReplayDevice::ReadImage(u64 id, VkImageAspectFlagBits aspect, u32 mip, u32 layer, std::vector<u8>& out) {
		auto found = images_.find(id);
		if (found == images_.end()) Fail("%s is not an image this replay has", model_.NameOf(id).c_str());
		ImageState& image = found->second;
		if (mip >= image.info.mipLevels || layer >= image.info.arrayLayers) Fail("%s has no mip %u, layer %u", model_.NameOf(id).c_str(), mip, layer);
		if (image.info.samples != VK_SAMPLE_COUNT_1_BIT) Fail("%s is multisampled; reading it is not supported yet", model_.NameOf(id).c_str());
		const FormatBlock block = BlockOf(image.info.format, aspect);
		if (!block.bytes) Fail("format %s cannot be read", EnumName(image.info.format) ? EnumName(image.info.format) : "?");
		const VkExtent3D extent = {std::max(1u, image.info.extent.width >> mip), std::max(1u, image.info.extent.height >> mip), std::max(1u, image.info.extent.depth >> mip)};
		const u64 bytes = (u64)((extent.width + block.width - 1) / block.width) * ((extent.height + block.height - 1) / block.height) * extent.depth * block.bytes;
		VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = bytes;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBuffer readback = VK_NULL_HANDLE;
		Check(vk.vkCreateBuffer(device, &info, nullptr, &readback), "vkCreateBuffer");
		VkMemoryRequirements needs = {};
		vk.vkGetBufferMemoryRequirements(device, readback, &needs);
		u32 type = MemoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
		if (type == UINT32_MAX) type = MemoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, needs.size, type};
		VkDeviceMemory memoryHere = VK_NULL_HANDLE;
		Check(vk.vkAllocateMemory(device, &allocate, nullptr, &memoryHere), "vkAllocateMemory");
		vk.vkBindBufferMemory(device, readback, memoryHere, 0);
		const size_t index = (size_t)layer * image.info.mipLevels + mip;
		const VkImageLayout layout = image.layouts[index];
		const VkImageAspectFlags aspects = AspectsOf(image.info.format);
		VkCommandBuffer cb = BeginOwn();
		VkImageMemoryBarrier toCopy = LayoutBarrier(image.image, aspects, mip, layer, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCopy);
		VkBufferImageCopy region = {};
		region.imageSubresource = {(VkImageAspectFlags)aspect, mip, layer, 1};
		region.imageExtent = extent;
		vk.vkCmdCopyImageToBuffer(cb, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
		const VkImageLayout back = layout == VK_IMAGE_LAYOUT_UNDEFINED || layout == VK_IMAGE_LAYOUT_PREINITIALIZED ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : layout;
		VkImageMemoryBarrier restore = LayoutBarrier(image.image, aspects, mip, layer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, back);
		vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &restore);
		SubmitOwn(cb);
		image.layouts[index] = back;
		void* mapped = nullptr;
		Check(vk.vkMapMemory(device, memoryHere, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
		out.assign((const u8*)mapped, (const u8*)mapped + bytes);
		vk.vkUnmapMemory(device, memoryHere);
		vk.vkDestroyBuffer(device, readback, nullptr);
		vk.vkFreeMemory(device, memoryHere, nullptr);
	}

	void ReplayDevice::ReadBuffer(u64 id, u64 offset, u64 size, std::vector<u8>& out) {
		auto found = buffers_.find(id);
		if (found == buffers_.end()) Fail("%s is not a buffer this replay has", model_.NameOf(id).c_str());
		if (offset >= found->second.size) {
			out.clear();
			return;
		}
		size = std::min(size, found->second.size - offset);
		if (!size) {
			out.clear();
			return;
		}
		VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = size;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBuffer readback = VK_NULL_HANDLE;
		Check(vk.vkCreateBuffer(device, &info, nullptr, &readback), "vkCreateBuffer");
		VkMemoryRequirements needs = {};
		vk.vkGetBufferMemoryRequirements(device, readback, &needs);
		const u32 type = MemoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, needs.size, type};
		VkDeviceMemory memoryHere = VK_NULL_HANDLE;
		Check(vk.vkAllocateMemory(device, &allocate, nullptr, &memoryHere), "vkAllocateMemory");
		vk.vkBindBufferMemory(device, readback, memoryHere, 0);
		VkCommandBuffer cb = BeginOwn();
		VkMemoryBarrier all = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
		vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &all, 0, nullptr, 0, nullptr);
		VkBufferCopy region = {offset, 0, size};
		vk.vkCmdCopyBuffer(cb, found->second.buffer, readback, 1, &region);
		SubmitOwn(cb);
		void* mapped = nullptr;
		Check(vk.vkMapMemory(device, memoryHere, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
		out.assign((const u8*)mapped, (const u8*)mapped + size);
		vk.vkUnmapMemory(device, memoryHere);
		vk.vkDestroyBuffer(device, readback, nullptr);
		vk.vkFreeMemory(device, memoryHere, nullptr);
	}
}
