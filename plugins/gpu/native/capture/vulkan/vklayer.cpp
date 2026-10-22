// The layer's entry points (the loader's interface: negotiation, the proc-address chain, instance and device creation)
// and the registry of instances, devices and their objects.
#include "vklayer.h"

#include <windows.h>
#include <atomic>
#include <cstring>
#include <shared_mutex>

namespace fb::capture::vulkan
{
	// The loader's dispatch table pointer, the first word of every dispatchable handle, keys its instance or device.
	static void* KeyOf(const void* dispatchable) { return *(void**)dispatchable; }

	struct Registry
	{
		std::shared_mutex lock;
		std::unordered_map<void*, std::unique_ptr<Instance>> instances;
		std::unordered_map<void*, std::unique_ptr<Device>> devices;
	};

	static Registry& Layer() {
		static Registry* registry = new Registry();
		return *registry;
	}

	u64 NextId() {
		static std::atomic<u64> next{1};
		return next++;
	}

	Device* DeviceOf(const void* dispatchable) {
		Registry& r = Layer();
		std::shared_lock<std::shared_mutex> guard(r.lock);
		auto found = r.devices.find(KeyOf(dispatchable));
		return found == r.devices.end() ? nullptr : found->second.get();
	}

	Instance* InstanceOf(const void* dispatchable) {
		Registry& r = Layer();
		std::shared_lock<std::shared_mutex> guard(r.lock);
		auto found = r.instances.find(KeyOf(dispatchable));
		return found == r.instances.end() ? nullptr : found->second.get();
	}

	std::vector<Device*> Devices() {
		Registry& r = Layer();
		std::shared_lock<std::shared_mutex> guard(r.lock);
		std::vector<Device*> out;
		for (auto& entry : r.devices) out.push_back(entry.second.get());
		return out;
	}

	void ManualStart(u64 frame) {
		for (Device* device : Devices()) device->frames.ManualStart(frame);
	}

	void ManualEnd() {
		for (Device* device : Devices()) device->frames.ManualEnd();
	}

	// --- Objects -----------------------------------------------------------------------------------------------------

	Object* Device::Add(u64 handle, u32 record, ObjectKind kind) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto object = std::make_unique<Object>();
		object->id = NextId();
		object->record = record;
		object->kind = kind;
		object->handle = handle;
		Object* raw = object.get();
		objects.push_back(std::move(object));
		byHandle[handle] = raw;
		byId[raw->id] = raw;
		return raw;
	}

	Object* Device::Find(u64 handle) {
		if (!handle) return nullptr;
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto found = byHandle.find(handle);
		return found == byHandle.end() ? nullptr : found->second;
	}

	Object* Device::FindId(u64 objectId) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto found = byId.find(objectId);
		return found == byId.end() ? nullptr : found->second;
	}

	void Device::Remove(u64 handle) {
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto found = byHandle.find(handle);
		if (found == byHandle.end()) return;
		Object* object = found->second;
		object->alive = false;
		byHandle.erase(found);
		frames.Destroyed(object);
	}

	// Destroyed objects leave the creation order (kept while a capture might still name them).
	void Device::Compact() {
		std::lock_guard<std::recursive_mutex> guard(lock);
		if (frames.Active()) return;
		std::vector<std::unique_ptr<Object>> kept;
		kept.reserve(objects.size());
		for (auto& object : objects) {
			if (object->alive) kept.push_back(std::move(object));
			else byId.erase(object->id);
		}
		objects.swap(kept);
	}

	u64 Device::Id(const void* handle) {
		if (handle == (const void*)this->handle) return id;
		Object* object = Find((u64)(uintptr_t)handle);
		return object ? object->id : 0;
	}

	// --- Image layouts ---------------------------------------------------------------------------------------------

	static void SetLayout(Object* image, const VkImageSubresourceRange& range, VkImageLayout layout) {
		const u32 mips = image->imageInfo.mipLevels, layers = image->imageInfo.arrayLayers;
		const u32 mipEnd = range.levelCount == VK_REMAINING_MIP_LEVELS ? mips : std::min(mips, range.baseMipLevel + range.levelCount);
		const u32 layerEnd = range.layerCount == VK_REMAINING_ARRAY_LAYERS ? layers : std::min(layers, range.baseArrayLayer + range.layerCount);
		for (u32 layer = range.baseArrayLayer; layer < layerEnd; ++layer)
			for (u32 mip = range.baseMipLevel; mip < mipEnd; ++mip) image->layouts[(size_t)layer * mips + mip] = layout;
	}

	void ApplyLayouts(Device& device, Recording& recording) {
		std::lock_guard<std::recursive_mutex> guard(device.lock);
		for (const LayoutChange& change : recording.layouts) {
			Object* image = device.FindId(change.image);
			if (image && image->alive && !image->layouts.empty()) SetLayout(image, change.range, change.layout);
		}
	}

	// --- Instance and device creation --------------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* out) {
		VkLayerInstanceCreateInfo* link = (VkLayerInstanceCreateInfo*)info->pNext;
		while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
			link = (VkLayerInstanceCreateInfo*)link->pNext;
		if (!link) return VK_ERROR_INITIALIZATION_FAILED;
		PFN_vkGetInstanceProcAddr nextGetProc = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
		link->u.pLayerInfo = link->u.pLayerInfo->pNext;
		auto create = (PFN_vkCreateInstance)nextGetProc(VK_NULL_HANDLE, "vkCreateInstance");
		const VkResult result = create(info, allocator, out);
		if (result != VK_SUCCESS) return result;
		auto instance = std::make_unique<Instance>();
		instance->handle = *out;
		instance->next.vkGetInstanceProcAddr = nextGetProc;
#define FB_VK_LOAD(name) instance->next.name = (PFN_##name)nextGetProc(*out, #name);
		FB_VK_INSTANCE_CALLS(FB_VK_LOAD)
#undef FB_VK_LOAD
		if (info->pApplicationInfo) {
			instance->apiVersion = info->pApplicationInfo->apiVersion;
			if (info->pApplicationInfo->pApplicationName) instance->application = info->pApplicationInfo->pApplicationName;
			if (info->pApplicationInfo->pEngineName) instance->engine = info->pApplicationInfo->pEngineName;
		}
		for (u32 i = 0; i < info->enabledExtensionCount; ++i) instance->extensions.push_back(info->ppEnabledExtensionNames[i]);
		Registry& r = Layer();
		std::unique_lock<std::shared_mutex> guard(r.lock);
		r.instances[KeyOf(*out)] = std::move(instance);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyInstance(VkInstance handle, const VkAllocationCallbacks* allocator) {
		Instance* instance = InstanceOf(handle);
		if (!instance) return;
		PFN_vkDestroyInstance destroy = instance->next.vkDestroyInstance;
		{
			Registry& r = Layer();
			std::unique_lock<std::shared_mutex> guard(r.lock);
			r.instances.erase(KeyOf(handle));
		}
		destroy(handle, allocator);
	}

	static std::string FixedText(const char* text, size_t capacity) { return std::string(text, strnlen(text, capacity)); }

	// The DeviceInfo record: where the device runs and how the program made it.
	static void DescribeDevice(Device& device, const VkDeviceCreateInfo& create) {
		Instance& instance = *device.instance;
		VkPhysicalDeviceProperties properties = {};
		instance.next.vkGetPhysicalDeviceProperties(device.physical, &properties);
		VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
		if (instance.next.vkGetPhysicalDeviceProperties2 && properties.apiVersion >= VK_API_VERSION_1_2) {
			VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
			instance.next.vkGetPhysicalDeviceProperties2(device.physical, &properties2);
		}
		instance.next.vkGetPhysicalDeviceMemoryProperties(device.physical, &device.memory);
		u32 familyCount = 0;
		instance.next.vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &familyCount, nullptr);
		device.families.resize(familyCount);
		instance.next.vkGetPhysicalDeviceQueueFamilyProperties(device.physical, &familyCount, device.families.data());

		std::vector<MemoryTypeInfo> types;
		for (u32 i = 0; i < device.memory.memoryTypeCount; ++i) types.push_back({device.memory.memoryTypes[i].propertyFlags, device.memory.memoryTypes[i].heapIndex});
		std::vector<const char*> extensions;
		for (const std::string& name : instance.extensions) extensions.push_back(name.c_str());
		DeviceInfo info = {};
		info.id = device.id;
		info.instanceApiVersion = instance.apiVersion;
		info.application = instance.application;
		info.engine = instance.engine;
		info.apiVersion = properties.apiVersion;
		info.driverVersion = properties.driverVersion;
		info.vendorId = properties.vendorID;
		info.deviceId = properties.deviceID;
		info.deviceType = (u32)properties.deviceType;
		info.deviceName = FixedText(properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
		info.driverName = FixedText(driver.driverName, VK_MAX_DRIVER_NAME_SIZE);
		info.driverInfo = FixedText(driver.driverInfo, VK_MAX_DRIVER_INFO_SIZE);
		memcpy(info.pipelineCacheUuid, properties.pipelineCacheUUID, 16);
		info.memoryTypeCount = (u32)types.size();
		info.memoryTypes = types.data();
		info.instanceExtensionCount = (u32)extensions.size();
		info.instanceExtensions = extensions.data();
		info.create = create;
		Ser s(device.info, &device);
		Serialize(s, info);
		TakeDroppedNext();
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info, const VkAllocationCallbacks* allocator, VkDevice* out) {
		Instance* instance = InstanceOf(physical);
		VkLayerDeviceCreateInfo* link = (VkLayerDeviceCreateInfo*)info->pNext;
		while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
			link = (VkLayerDeviceCreateInfo*)link->pNext;
		if (!link || !instance) return VK_ERROR_INITIALIZATION_FAILED;
		PFN_vkGetInstanceProcAddr nextInstanceProc = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
		PFN_vkGetDeviceProcAddr nextDeviceProc = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
		link->u.pLayerInfo = link->u.pLayerInfo->pNext;
		PFN_vkSetDeviceLoaderData setLoaderData = nullptr;
		for (auto* data = (const VkLayerDeviceCreateInfo*)info->pNext; data; data = (const VkLayerDeviceCreateInfo*)data->pNext)
			if (data->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && data->function == VK_LOADER_DATA_CALLBACK) setLoaderData = data->u.pfnSetDeviceLoaderData;
		auto create = (PFN_vkCreateDevice)nextInstanceProc(instance->handle, "vkCreateDevice");
		const VkResult result = create(physical, info, allocator, out);
		if (result != VK_SUCCESS) return result;
		auto device = std::make_unique<Device>();
		device->handle = *out;
		device->physical = physical;
		device->instance = instance;
		device->setLoaderData = setLoaderData;
		device->id = NextId();
		device->next.vkGetDeviceProcAddr = nextDeviceProc;
#define FB_VK_LOAD(name) device->next.name = (PFN_##name)nextDeviceProc(*out, #name);
		FB_VK_DEVICE_HOOKS(FB_VK_LOAD)
		FB_VK_DEVICE_CALLS(FB_VK_LOAD)
#undef FB_VK_LOAD
		DescribeDevice(*device, *info);
		{
			Registry& r = Layer();
			std::unique_lock<std::shared_mutex> guard(r.lock);
			r.devices[KeyOf(*out)] = std::move(device);
		}
		Session::Get().NoteApi("Vulkan");
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyDevice(VkDevice handle, const VkAllocationCallbacks* allocator) {
		Device* device = DeviceOf(handle);
		if (!device) return;
		PFN_vkDestroyDevice destroy = device->next.vkDestroyDevice;
		std::unique_ptr<Device> owned;
		{
			Registry& r = Layer();
			std::unique_lock<std::shared_mutex> guard(r.lock);
			auto found = r.devices.find(KeyOf(handle));
			if (found != r.devices.end()) {
				owned = std::move(found->second);
				r.devices.erase(found);
			}
		}
		owned.reset();
		destroy(handle, allocator);
	}

	// --- The proc-address chain ----------------------------------------------------------------------------------

	static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetDeviceProcAddr(VkDevice handle, const char* name);
	static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetInstanceProcAddr(VkInstance handle, const char* name);

	static PFN_vkVoidFunction OwnFunction(const char* name) {
		if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)hook_vkGetInstanceProcAddr;
		if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)hook_vkGetDeviceProcAddr;
		if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)hook_vkCreateInstance;
		if (!strcmp(name, "vkDestroyInstance")) return (PFN_vkVoidFunction)hook_vkDestroyInstance;
		if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)hook_vkCreateDevice;
		if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)hook_vkDestroyDevice;
		return HookFor(name);
	}

	static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetDeviceProcAddr(VkDevice handle, const char* name) {
		Device* device = handle ? DeviceOf(handle) : nullptr;
		if (!device) return nullptr;
		PFN_vkVoidFunction own = OwnFunction(name);
		// Only what the next layer has: a hook for a function the device does not have would call null.
		PFN_vkVoidFunction next = device->next.vkGetDeviceProcAddr(handle, name);
		return own && next ? own : next;
	}

	static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hook_vkGetInstanceProcAddr(VkInstance handle, const char* name) {
		PFN_vkVoidFunction own = OwnFunction(name);
		if (!handle) return own;
		Instance* instance = InstanceOf(handle);
		if (!instance) return own;
		PFN_vkVoidFunction next = instance->next.vkGetInstanceProcAddr(handle, name);
		if (!strcmp(name, "vkCreateDevice") || !strcmp(name, "vkDestroyInstance") || !strcmp(name, "vkGetInstanceProcAddr") ||
			!strcmp(name, "vkGetDeviceProcAddr"))
			return own;
		return own && next ? own : next;
	}
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version) {
	if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
	if (version->loaderLayerInterfaceVersion > 2) version->loaderLayerInterfaceVersion = 2;
	version->pfnGetInstanceProcAddr = fb::capture::vulkan::hook_vkGetInstanceProcAddr;
	version->pfnGetDeviceProcAddr = fb::capture::vulkan::hook_vkGetDeviceProcAddr;
	version->pfnGetPhysicalDeviceProcAddr = nullptr;
	return VK_SUCCESS;
}
