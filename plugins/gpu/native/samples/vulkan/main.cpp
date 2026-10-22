// fb-sample-vulkan: the Vulkan twin of fb-sample-d3d12 (docs/22), to capture through the Flashbang layer: a compute
// pass writing a storage image, textured cubes and a floor in a render pass into an HDR target with depth, then a
// tonemap with dynamic rendering and synchronization-2 barriers to the swap chain; debug-utils labels and names, a
// persistently mapped uniform buffer, geometry uploaded through staging, push constants. Shaders come from
// make_shaders.py. The GPU debugger's tests capture it with the layer on (VK_INSTANCE_LAYERS=VK_LAYER_IED_flashbang).
//   fb-sample-vulkan [--frames N] [--capture-frame K] [--flashbang path\to\flashbang.dll] [--hidden] [--screenshot last.bmp]
// --screenshot writes the last frame as the program saw it, for comparing with its replay.
#include "include/flashbang_app.h"
#include "shaders.h"

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
	const uint32_t kWidth = 1280, kHeight = 720, kPattern = 256, kChecker = 256;
	const VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT, kDepthFormat = VK_FORMAT_D32_SFLOAT, kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

#define SAMPLE_INSTANCE_FUNCTIONS(X) \
	X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkCreateWin32SurfaceKHR) X(vkDestroySurfaceKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)

#define SAMPLE_DEVICE_FUNCTIONS(X) \
	X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) \
	X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage) \
	X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
	X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkCreateDescriptorPool) \
	X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateRenderPass) X(vkCreateFramebuffer) X(vkCreateGraphicsPipelines) \
	X(vkCreateComputePipelines) X(vkCreateCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
	X(vkResetCommandBuffer) X(vkCreateFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkCmdPipelineBarrier) \
	X(vkCmdPipelineBarrier2) X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
	X(vkCmdDispatch) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdSetViewport) \
	X(vkCmdSetScissor) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdPushConstants) X(vkCmdDrawIndexed) X(vkCmdDraw) \
	X(vkCmdBeginDebugUtilsLabelEXT) X(vkCmdEndDebugUtilsLabelEXT) X(vkSetDebugUtilsObjectNameEXT)

	struct Api
	{
		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
		PFN_vkCreateInstance vkCreateInstance = nullptr;
#define SAMPLE_DECLARE(name) PFN_##name name = nullptr;
		SAMPLE_INSTANCE_FUNCTIONS(SAMPLE_DECLARE)
		SAMPLE_DEVICE_FUNCTIONS(SAMPLE_DECLARE)
#undef SAMPLE_DECLARE
	};

	void Check(VkResult result, const char* what) {
		if (result == VK_SUCCESS) return;
		fprintf(stderr, "fb-sample-vulkan: %s failed (VkResult %d)\n", what, (int)result);
		ExitProcess(2);
	}

	struct Float4x4
	{
		float m[4][4];
	};

	Float4x4 Multiply(const Float4x4& a, const Float4x4& b) {
		Float4x4 r = {};
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
		return r;
	}

	Float4x4 Identity() {
		Float4x4 r = {};
		for (int i = 0; i < 4; ++i) r.m[i][i] = 1;
		return r;
	}

	// Column-vector convention (clip = M * v); SPIR-V reads the blocks column-major, so matrices go up transposed.
	Float4x4 Transposed(const Float4x4& a) {
		Float4x4 r;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[j][i];
		return r;
	}

	Float4x4 Translation(float x, float y, float z) {
		Float4x4 r = Identity();
		r.m[0][3] = x, r.m[1][3] = y, r.m[2][3] = z;
		return r;
	}

	Float4x4 Scale(float x, float y, float z) {
		Float4x4 r = Identity();
		r.m[0][0] = x, r.m[1][1] = y, r.m[2][2] = z;
		return r;
	}

	Float4x4 RotationY(float angle) {
		Float4x4 r = Identity();
		r.m[0][0] = std::cos(angle), r.m[0][2] = std::sin(angle);
		r.m[2][0] = -std::sin(angle), r.m[2][2] = std::cos(angle);
		return r;
	}

	Float4x4 Perspective(float fovY, float aspect, float zn, float zf) {
		const float y = 1.0f / std::tan(fovY / 2), x = y / aspect;
		Float4x4 r = {};
		r.m[0][0] = x, r.m[1][1] = y;
		r.m[2][2] = zf / (zf - zn), r.m[2][3] = -zn * zf / (zf - zn);
		r.m[3][2] = 1;
		return r;
	}

	Float4x4 View(float eyeX, float eyeY, float eyeZ) {
		float fx = -eyeX, fy = -eyeY, fz = -eyeZ;
		const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
		fx /= fl, fy /= fl, fz /= fl;
		float rx = fz, ry = 0, rz = -fx;
		const float rl = std::sqrt(rx * rx + rz * rz);
		rx /= rl, rz /= rl;
		const float ux = fy * rz - fz * ry, uy = fz * rx - fx * rz, uz = fx * ry - fy * rx;
		Float4x4 r = Identity();
		r.m[0][0] = rx, r.m[0][1] = ry, r.m[0][2] = rz, r.m[0][3] = -(rx * eyeX + ry * eyeY + rz * eyeZ);
		r.m[1][0] = ux, r.m[1][1] = uy, r.m[1][2] = uz, r.m[1][3] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
		r.m[2][0] = fx, r.m[2][1] = fy, r.m[2][2] = fz, r.m[2][3] = -(fx * eyeX + fy * eyeY + fz * eyeZ);
		return r;
	}

	struct Vertex
	{
		float pos[3];
		float normal[3];
		float uv[2];
	};

	struct ObjectConstants
	{
		Float4x4 world;
		float tint[4];
		float material[4];   // x: 0 the pattern, 1 the checker
	};

	struct FrameConstants
	{
		Float4x4 viewProj;
		float light[4];      // direction and intensity
	};

	void MakeGeometry(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
		const float faces[6][3][3] = {
			{{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
			{{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
		};
		for (int face = 0; face < 6; ++face) {
			const float* nf = faces[face][0];
			const float* u = faces[face][1];
			const float* v = faces[face][2];
			const uint16_t base = (uint16_t)vertices.size();
			const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
			for (auto& c : corners) {
				Vertex vertex = {};
				for (int k = 0; k < 3; ++k) {
					vertex.pos[k] = 0.5f * (nf[k] + c[0] * u[k] + c[1] * v[k]);
					vertex.normal[k] = nf[k];
				}
				vertex.uv[0] = 0.5f + 0.5f * c[0];
				vertex.uv[1] = 0.5f - 0.5f * c[1];
				vertices.push_back(vertex);
			}
			const uint16_t quad[6] = {0, 2, 1, 0, 3, 2};
			for (uint16_t q : quad) indices.push_back((uint16_t)(base + q));
		}
		const uint16_t base = (uint16_t)vertices.size();
		const float floor[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (auto& c : floor) vertices.push_back({{c[0], 0, c[1]}, {0, 1, 0}, {0.5f + 0.5f * c[0], 0.5f + 0.5f * c[1]}});
		const uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
		for (uint16_t q : quad) indices.push_back((uint16_t)(base + q));
	}

	// A top-down 32-bit BMP of BGRA rows.
	bool WriteBmp(const wchar_t* path, uint32_t width, uint32_t height, const uint8_t* bgra) {
		FILE* file = _wfopen(path, L"wb");
		if (!file) return false;
		const uint32_t pixels = width * height * 4;
		uint8_t header[54] = {'B', 'M'};
		auto put32 = [&](int at, uint32_t v) { memcpy(header + at, &v, 4); };
		put32(2, 54 + pixels);
		put32(10, 54);
		put32(14, 40);
		put32(18, width);
		put32(22, (uint32_t)-(int32_t)height);
		header[26] = 1;
		header[28] = 32;
		put32(34, pixels);
		const bool ok = fwrite(header, 1, 54, file) == 54 && fwrite(bgra, 1, pixels, file) == pixels;
		fclose(file);
		return ok;
	}

	LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
		if (message == WM_DESTROY) {
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(window, message, w, l);
	}

	// The device, its queue and what the sample keeps.
	struct Gpu
	{
		Api vk;
		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice physical = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		uint32_t family = 0;
		VkQueue queue = VK_NULL_HANDLE;
		VkPhysicalDeviceMemoryProperties memory = {};
		VkCommandPool pool = VK_NULL_HANDLE;
	};

	void Name(Gpu& g, VkObjectType type, uint64_t handle, const char* name) {
		if (!g.vk.vkSetDebugUtilsObjectNameEXT) return;
		VkDebugUtilsObjectNameInfoEXT info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
		info.objectType = type;
		info.objectHandle = handle;
		info.pObjectName = name;
		g.vk.vkSetDebugUtilsObjectNameEXT(g.device, &info);
	}

	void Label(Gpu& g, VkCommandBuffer cb, const char* name) {
		if (!g.vk.vkCmdBeginDebugUtilsLabelEXT) return;
		VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
		label.pLabelName = name;
		g.vk.vkCmdBeginDebugUtilsLabelEXT(cb, &label);
	}

	void EndLabel(Gpu& g, VkCommandBuffer cb) {
		if (g.vk.vkCmdEndDebugUtilsLabelEXT) g.vk.vkCmdEndDebugUtilsLabelEXT(cb);
	}

	uint32_t MemoryType(const Gpu& g, uint32_t bits, VkMemoryPropertyFlags wanted) {
		for (uint32_t i = 0; i < g.memory.memoryTypeCount; ++i)
			if ((bits & (1u << i)) && (g.memory.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
		fprintf(stderr, "fb-sample-vulkan: no memory type\n");
		ExitProcess(2);
	}

	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		void* mapped = nullptr;
	};

	Buffer MakeBuffer(Gpu& g, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, const char* name) {
		Buffer b;
		VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = size;
		info.usage = usage;
		Check(g.vk.vkCreateBuffer(g.device, &info, nullptr, &b.buffer), "vkCreateBuffer");
		VkMemoryRequirements needs;
		g.vk.vkGetBufferMemoryRequirements(g.device, b.buffer, &needs);
		VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate.allocationSize = needs.size;
		allocate.memoryTypeIndex = MemoryType(g, needs.memoryTypeBits, properties);
		Check(g.vk.vkAllocateMemory(g.device, &allocate, nullptr, &b.memory), "vkAllocateMemory");
		Check(g.vk.vkBindBufferMemory(g.device, b.buffer, b.memory, 0), "vkBindBufferMemory");
		if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) Check(g.vk.vkMapMemory(g.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "vkMapMemory");
		if (name) Name(g, VK_OBJECT_TYPE_BUFFER, (uint64_t)b.buffer, name);
		return b;
	}

	struct Image
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};

	Image MakeImage(Gpu& g, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, const char* name) {
		Image image;
		VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = format;
		info.extent = {width, height, 1};
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = usage;
		Check(g.vk.vkCreateImage(g.device, &info, nullptr, &image.image), "vkCreateImage");
		VkMemoryRequirements needs;
		g.vk.vkGetImageMemoryRequirements(g.device, image.image, &needs);
		VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate.allocationSize = needs.size;
		allocate.memoryTypeIndex = MemoryType(g, needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		Check(g.vk.vkAllocateMemory(g.device, &allocate, nullptr, &image.memory), "vkAllocateMemory");
		Check(g.vk.vkBindImageMemory(g.device, image.image, image.memory, 0), "vkBindImageMemory");
		VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image = image.image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.subresourceRange = {aspect, 0, 1, 0, 1};
		Check(g.vk.vkCreateImageView(g.device, &view, nullptr, &image.view), "vkCreateImageView");
		Name(g, VK_OBJECT_TYPE_IMAGE, (uint64_t)image.image, name);
		return image;
	}

	VkShaderModule Shader(Gpu& g, const uint32_t* code, size_t bytes) {
		VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = bytes;
		info.pCode = code;
		VkShaderModule module = VK_NULL_HANDLE;
		Check(g.vk.vkCreateShaderModule(g.device, &info, nullptr, &module), "vkCreateShaderModule");
		return module;
	}

	VkImageMemoryBarrier Barrier(VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
		VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.srcAccessMask = src;
		barrier.dstAccessMask = dst;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {aspect, 0, 1, 0, 1};
		return barrier;
	}

	VkImageMemoryBarrier2 Barrier2(VkImage image, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage, VkAccessFlags2 src,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dst) {
		VkImageMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		barrier.srcStageMask = srcStage;
		barrier.srcAccessMask = src;
		barrier.dstStageMask = dstStage;
		barrier.dstAccessMask = dst;
		barrier.oldLayout = from;
		barrier.newLayout = to;
		barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		return barrier;
	}

	void Dependency2(Gpu& g, VkCommandBuffer cb, const VkImageMemoryBarrier2& barrier) {
		VkDependencyInfo info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		info.imageMemoryBarrierCount = 1;
		info.pImageMemoryBarriers = &barrier;
		g.vk.vkCmdPipelineBarrier2(cb, &info);
	}

	// Runs `record` on a one-time command buffer and waits for it.
	template <class F> void Once(Gpu& g, F record) {
		VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = g.pool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cb = VK_NULL_HANDLE;
		Check(g.vk.vkAllocateCommandBuffers(g.device, &allocate, &cb), "vkAllocateCommandBuffers");
		VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		Check(g.vk.vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer");
		record(cb);
		Check(g.vk.vkEndCommandBuffer(cb), "vkEndCommandBuffer");
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cb;
		Check(g.vk.vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
		Check(g.vk.vkQueueWaitIdle(g.queue), "vkQueueWaitIdle");
	}

	bool LoadVulkan(Api& vk) {
		HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
		if (!loader) return false;
		vk.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(loader, "vkGetInstanceProcAddr");
		vk.vkCreateInstance = vk.vkGetInstanceProcAddr ? (PFN_vkCreateInstance)vk.vkGetInstanceProcAddr(nullptr, "vkCreateInstance") : nullptr;
		return vk.vkCreateInstance != nullptr;
	}

	void MakeDevice(Gpu& g) {
		const char* instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
		VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "fb-sample-vulkan";
		app.apiVersion = VK_API_VERSION_1_3;
		VkInstanceCreateInfo instanceInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		instanceInfo.pApplicationInfo = &app;
		instanceInfo.enabledExtensionCount = 3;
		instanceInfo.ppEnabledExtensionNames = instanceExtensions;
		Check(g.vk.vkCreateInstance(&instanceInfo, nullptr, &g.instance), "vkCreateInstance");
#define SAMPLE_LOAD(name) g.vk.name = (PFN_##name)g.vk.vkGetInstanceProcAddr(g.instance, #name);
		SAMPLE_INSTANCE_FUNCTIONS(SAMPLE_LOAD)
#undef SAMPLE_LOAD
		uint32_t count = 0;
		g.vk.vkEnumeratePhysicalDevices(g.instance, &count, nullptr);
		std::vector<VkPhysicalDevice> devices(count);
		g.vk.vkEnumeratePhysicalDevices(g.instance, &count, devices.data());
		for (VkPhysicalDevice candidate : devices) {
			VkPhysicalDeviceProperties properties;
			g.vk.vkGetPhysicalDeviceProperties(candidate, &properties);
			if (properties.apiVersion < VK_API_VERSION_1_3) continue;
			if (!g.physical || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) g.physical = candidate;
		}
		if (!g.physical) {
			fprintf(stderr, "fb-sample-vulkan: no Vulkan 1.3 device\n");
			ExitProcess(2);
		}
		uint32_t families = 0;
		g.vk.vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &families, nullptr);
		std::vector<VkQueueFamilyProperties> familyProperties(families);
		g.vk.vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &families, familyProperties.data());
		for (uint32_t i = 0; i < families; ++i)
			if ((familyProperties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
				g.family = i;
				break;
			}
		g.vk.vkGetPhysicalDeviceMemoryProperties(g.physical, &g.memory);
		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queueInfo.queueFamilyIndex = g.family;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &priority;
		VkPhysicalDeviceVulkan13Features features13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;
		const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
		VkDeviceCreateInfo deviceInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		deviceInfo.pNext = &features13;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = 1;
		deviceInfo.ppEnabledExtensionNames = deviceExtensions;
		Check(g.vk.vkCreateDevice(g.physical, &deviceInfo, nullptr, &g.device), "vkCreateDevice");
#define SAMPLE_LOAD(name) g.vk.name = (PFN_##name)g.vk.vkGetDeviceProcAddr(g.device, #name);
		SAMPLE_DEVICE_FUNCTIONS(SAMPLE_LOAD)
#undef SAMPLE_LOAD
		g.vk.vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
		Name(g, VK_OBJECT_TYPE_QUEUE, (uint64_t)g.queue, "Graphics queue");
		VkCommandPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = g.family;
		Check(g.vk.vkCreateCommandPool(g.device, &poolInfo, nullptr, &g.pool), "vkCreateCommandPool");
	}
}

int wmain(int argc, wchar_t** argv) {
	int frames = 0, captureFrame = -1;
	const wchar_t* libraryPath = nullptr;
	bool hidden = false;
	const wchar_t* screenshot = nullptr;
	for (int i = 1; i < argc; ++i) {
		if (!wcscmp(argv[i], L"--frames") && i + 1 < argc) frames = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--capture-frame") && i + 1 < argc) captureFrame = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--flashbang") && i + 1 < argc) libraryPath = argv[++i];
		else if (!wcscmp(argv[i], L"--hidden")) hidden = true;
		else if (!wcscmp(argv[i], L"--screenshot") && i + 1 < argc) screenshot = argv[++i];
	}

	// The layer captures; the library (the same DLL) is loaded here only to trigger a capture from the program.
	flashbang::Library capture;
	const bool captureLoaded = flashbang::Load(capture, libraryPath);
	printf("fb-sample-vulkan: capture library %s\n", captureLoaded ? "loaded" : "not loaded");
	fflush(stdout);

	Gpu g;
	if (!LoadVulkan(g.vk)) {
		fprintf(stderr, "fb-sample-vulkan: no Vulkan loader\n");
		return 2;
	}
	MakeDevice(g);

	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = L"FlashbangSampleVulkan";
	windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassW(&windowClass);
	RECT rect = {0, 0, (LONG)kWidth, (LONG)kHeight};
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
	HWND window = CreateWindowW(windowClass.lpszClassName, L"Flashbang sample (Vulkan)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, windowClass.hInstance, nullptr);
	ShowWindow(window, hidden ? SW_SHOWMINNOACTIVE : SW_SHOW);

	VkWin32SurfaceCreateInfoKHR surfaceInfo = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
	surfaceInfo.hinstance = windowClass.hInstance;
	surfaceInfo.hwnd = window;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	Check(g.vk.vkCreateWin32SurfaceKHR(g.instance, &surfaceInfo, nullptr, &surface), "vkCreateWin32SurfaceKHR");
	VkBool32 supported = VK_FALSE;
	g.vk.vkGetPhysicalDeviceSurfaceSupportKHR(g.physical, g.family, surface, &supported);
	if (!supported) {
		fprintf(stderr, "fb-sample-vulkan: the queue cannot present\n");
		return 2;
	}
	VkSurfaceCapabilitiesKHR caps;
	g.vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.physical, surface, &caps);
	uint32_t formatCount = 0;
	g.vk.vkGetPhysicalDeviceSurfaceFormatsKHR(g.physical, surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	g.vk.vkGetPhysicalDeviceSurfaceFormatsKHR(g.physical, surface, &formatCount, formats.data());
	VkSurfaceFormatKHR surfaceFormat = formats[0];
	for (const VkSurfaceFormatKHR& f : formats)
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM) surfaceFormat = f;
	VkSwapchainCreateInfoKHR chainInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	chainInfo.surface = surface;
	chainInfo.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
	chainInfo.imageFormat = surfaceFormat.format;
	chainInfo.imageColorSpace = surfaceFormat.colorSpace;
	chainInfo.imageExtent = {kWidth, kHeight};
	chainInfo.imageArrayLayers = 1;
	chainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (screenshot ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
	chainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	chainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	chainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	chainInfo.clipped = VK_TRUE;
	VkSwapchainKHR chain = VK_NULL_HANDLE;
	Check(g.vk.vkCreateSwapchainKHR(g.device, &chainInfo, nullptr, &chain), "vkCreateSwapchainKHR");
	uint32_t imageCount = 0;
	g.vk.vkGetSwapchainImagesKHR(g.device, chain, &imageCount, nullptr);
	std::vector<VkImage> backBuffers(imageCount);
	g.vk.vkGetSwapchainImagesKHR(g.device, chain, &imageCount, backBuffers.data());
	std::vector<VkImageView> backViews(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i) {
		VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image = backBuffers[i];
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = surfaceFormat.format;
		view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		Check(g.vk.vkCreateImageView(g.device, &view, nullptr, &backViews[i]), "back buffer view");
	}

	// Resources.
	Image hdr = MakeImage(g, kWidth, kHeight, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "HDR");
	Image depth = MakeImage(g, kWidth, kHeight, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, "Depth");
	Image pattern = MakeImage(g, kPattern, kPattern, kTextureFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "Pattern");
	Image checker = MakeImage(g, kChecker, kChecker, kTextureFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "Checker");

	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
	MakeGeometry(vertices, indices);
	const VkDeviceSize vertexBytes = vertices.size() * sizeof(Vertex), indexBytes = indices.size() * sizeof(uint16_t);
	Buffer geometry = MakeBuffer(g, vertexBytes + indexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "Cube geometry");
	Buffer staging = MakeBuffer(g, vertexBytes + indexBytes + kChecker * kChecker * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Staging");
	memcpy(staging.mapped, vertices.data(), vertexBytes);
	memcpy((uint8_t*)staging.mapped + vertexBytes, indices.data(), indexBytes);
	uint8_t* checkerTexels = (uint8_t*)staging.mapped + vertexBytes + indexBytes;
	for (uint32_t y = 0; y < kChecker; ++y)
		for (uint32_t x = 0; x < kChecker; ++x) {
			const uint8_t value = ((x / 32 + y / 32) & 1) ? 230 : 90;
			uint8_t* texel = checkerTexels + (y * kChecker + x) * 4;
			texel[0] = texel[1] = texel[2] = value;
			texel[3] = 255;
		}
	Buffer constants = MakeBuffer(g, sizeof(FrameConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "Frame constants");
	FrameConstants* frameConstants = (FrameConstants*)constants.mapped;

	Once(g, [&](VkCommandBuffer cb) {
		VkBufferCopy copy = {0, 0, vertexBytes + indexBytes};
		g.vk.vkCmdCopyBuffer(cb, staging.buffer, geometry.buffer, 1, &copy);
		VkImageMemoryBarrier toCopy = Barrier(checker.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
		g.vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCopy);
		VkBufferImageCopy region = {};
		region.bufferOffset = vertexBytes + indexBytes;
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {kChecker, kChecker, 1};
		g.vk.vkCmdCopyBufferToImage(cb, staging.buffer, checker.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		VkImageMemoryBarrier toRead[2] = {
			Barrier(checker.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
			Barrier(pattern.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT)};
		g.vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, toRead);
	});

	VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	VkSampler sampler = VK_NULL_HANDLE;
	Check(g.vk.vkCreateSampler(g.device, &samplerInfo, nullptr, &sampler), "vkCreateSampler");
	Name(g, VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "Linear wrap");

	// Descriptor layouts: the pattern's storage image; the scene's constants and two textures; the tonemap's HDR input.
	auto setLayout = [&](std::initializer_list<VkDescriptorSetLayoutBinding> bindings) {
		VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		info.bindingCount = (uint32_t)bindings.size();
		info.pBindings = bindings.begin();
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		Check(g.vk.vkCreateDescriptorSetLayout(g.device, &info, nullptr, &layout), "vkCreateDescriptorSetLayout");
		return layout;
	};
	const VkShaderStageFlags graphics = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayout computeLayout = setLayout({{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}});
	VkDescriptorSetLayout sceneLayout = setLayout({{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, graphics, nullptr},
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
		{2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
	VkDescriptorSetLayout tonemapLayout = setLayout({{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});

	auto pipelineLayout = [&](VkDescriptorSetLayout set, uint32_t pushBytes) {
		VkPushConstantRange range = {graphics, 0, pushBytes};
		VkPipelineLayoutCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		info.setLayoutCount = 1;
		info.pSetLayouts = &set;
		info.pushConstantRangeCount = pushBytes ? 1 : 0;
		info.pPushConstantRanges = &range;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		Check(g.vk.vkCreatePipelineLayout(g.device, &info, nullptr, &layout), "vkCreatePipelineLayout");
		return layout;
	};
	VkPipelineLayout computePipelineLayout = pipelineLayout(computeLayout, 0);
	VkPipelineLayout scenePipelineLayout = pipelineLayout(sceneLayout, sizeof(ObjectConstants));
	VkPipelineLayout tonemapPipelineLayout = pipelineLayout(tonemapLayout, 0);

	VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3}};
	VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolInfo.maxSets = 3;
	poolInfo.poolSizeCount = 3;
	poolInfo.pPoolSizes = poolSizes;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	Check(g.vk.vkCreateDescriptorPool(g.device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
	VkDescriptorSetLayout layouts[3] = {computeLayout, sceneLayout, tonemapLayout};
	VkDescriptorSetAllocateInfo setInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	setInfo.descriptorPool = descriptorPool;
	setInfo.descriptorSetCount = 3;
	setInfo.pSetLayouts = layouts;
	VkDescriptorSet sets[3];
	Check(g.vk.vkAllocateDescriptorSets(g.device, &setInfo, sets), "vkAllocateDescriptorSets");
	Name(g, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)sets[1], "Scene bindings");
	VkDescriptorImageInfo patternStorage = {VK_NULL_HANDLE, pattern.view, VK_IMAGE_LAYOUT_GENERAL};
	VkDescriptorBufferInfo constantsInfo = {constants.buffer, 0, sizeof(FrameConstants)};
	VkDescriptorImageInfo patternTexture = {sampler, pattern.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkDescriptorImageInfo checkerTexture = {sampler, checker.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkDescriptorImageInfo hdrTexture = {sampler, hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	auto write = [](VkDescriptorSet set, uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* image, const VkDescriptorBufferInfo* buffer) {
		VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		w.dstSet = set;
		w.dstBinding = binding;
		w.descriptorCount = 1;
		w.descriptorType = type;
		w.pImageInfo = image;
		w.pBufferInfo = buffer;
		return w;
	};
	VkWriteDescriptorSet writes[] = {
		write(sets[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &patternStorage, nullptr),
		write(sets[1], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &constantsInfo),
		write(sets[1], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &patternTexture, nullptr),
		write(sets[1], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &checkerTexture, nullptr),
		write(sets[2], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &hdrTexture, nullptr),
	};
	g.vk.vkUpdateDescriptorSets(g.device, 5, writes, 0, nullptr);

	// The scene's render pass: HDR and depth cleared, HDR left ready to sample.
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = kHdrFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[1] = attachments[0];
	attachments[1].format = kDepthFormat;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;
	VkSubpassDependency dependency = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0};
	VkRenderPassCreateInfo passInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	passInfo.attachmentCount = 2;
	passInfo.pAttachments = attachments;
	passInfo.subpassCount = 1;
	passInfo.pSubpasses = &subpass;
	passInfo.dependencyCount = 1;
	passInfo.pDependencies = &dependency;
	VkRenderPass scenePass = VK_NULL_HANDLE;
	Check(g.vk.vkCreateRenderPass(g.device, &passInfo, nullptr, &scenePass), "vkCreateRenderPass");
	Name(g, VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)scenePass, "Scene pass");
	VkImageView sceneViews[2] = {hdr.view, depth.view};
	VkFramebufferCreateInfo framebufferInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
	framebufferInfo.renderPass = scenePass;
	framebufferInfo.attachmentCount = 2;
	framebufferInfo.pAttachments = sceneViews;
	framebufferInfo.width = kWidth;
	framebufferInfo.height = kHeight;
	framebufferInfo.layers = 1;
	VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
	Check(g.vk.vkCreateFramebuffer(g.device, &framebufferInfo, nullptr, &sceneFramebuffer), "vkCreateFramebuffer");

	// Pipelines.
	VkShaderModule patternCs = Shader(g, kPatternCs, sizeof(kPatternCs)), sceneVs = Shader(g, kSceneVs, sizeof(kSceneVs)),
		sceneFs = Shader(g, kSceneFs, sizeof(kSceneFs)), tonemapVs = Shader(g, kTonemapVs, sizeof(kTonemapVs)),
		tonemapFs = Shader(g, kTonemapFs, sizeof(kTonemapFs));
	VkComputePipelineCreateInfo computeInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	computeInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, patternCs, "main"};
	computeInfo.layout = computePipelineLayout;
	VkPipeline computePipeline = VK_NULL_HANDLE;
	Check(g.vk.vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &computeInfo, nullptr, &computePipeline), "vkCreateComputePipelines");
	Name(g, VK_OBJECT_TYPE_PIPELINE, (uint64_t)computePipeline, "Pattern");

	auto graphicsPipeline = [&](VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, bool scene, const char* name) {
		VkPipelineShaderStageCreateInfo stages[2] = {
			{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main"},
			{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main"}};
		VkVertexInputBindingDescription binding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
		VkVertexInputAttributeDescription attributes[3] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
		VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		if (scene) {
			input.vertexBindingDescriptionCount = 1;
			input.pVertexBindingDescriptions = &binding;
			input.vertexAttributeDescriptionCount = 3;
			input.pVertexAttributeDescriptions = attributes;
		}
		VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		viewport.viewportCount = viewport.scissorCount = 1;
		VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
		raster.lineWidth = 1.0f;
		VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		VkPipelineDepthStencilStateCreateInfo depthState = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		depthState.depthTestEnable = depthState.depthWriteEnable = scene ? VK_TRUE : VK_FALSE;
		depthState.depthCompareOp = VK_COMPARE_OP_LESS;
		VkPipelineColorBlendAttachmentState blendAttachment = {};
		blendAttachment.colorWriteMask = 0xF;
		VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;
		VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = dynamics;
		VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachmentFormats = &surfaceFormat.format;
		VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		info.pNext = scene ? nullptr : &rendering;
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &multisample;
		info.pDepthStencilState = &depthState;
		info.pColorBlendState = &blend;
		info.pDynamicState = &dynamic;
		info.layout = layout;
		info.renderPass = scene ? scenePass : VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		Check(g.vk.vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), name);
		Name(g, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, name);
		return pipeline;
	};
	VkPipeline litPipeline = graphicsPipeline(sceneVs, sceneFs, scenePipelineLayout, true, "Lit");
	VkPipeline tonemapPipeline = graphicsPipeline(tonemapVs, tonemapFs, tonemapPipelineLayout, false, "Tonemap");

	VkCommandBufferAllocateInfo cbInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	cbInfo.commandPool = g.pool;
	cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbInfo.commandBufferCount = 1;
	VkCommandBuffer cb = VK_NULL_HANDLE;
	Check(g.vk.vkAllocateCommandBuffers(g.device, &cbInfo, &cb), "frame command buffer");
	Name(g, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)cb, "Frame commands");
	VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence frameDone = VK_NULL_HANDLE;
	Check(g.vk.vkCreateFence(g.device, &fenceInfo, nullptr, &frameDone), "vkCreateFence");
	VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore acquired = VK_NULL_HANDLE, rendered = VK_NULL_HANDLE;
	Check(g.vk.vkCreateSemaphore(g.device, &semaphoreInfo, nullptr, &acquired), "vkCreateSemaphore");
	Check(g.vk.vkCreateSemaphore(g.device, &semaphoreInfo, nullptr, &rendered), "vkCreateSemaphore");

	const Float4x4 viewProj = Multiply(Perspective(0.9f, (float)kWidth / kHeight, 0.1f, 50.0f), View(0.0f, 2.2f, -4.5f));
	struct Item
	{
		const char* name;
		Float4x4 world;
		float tint[3];
		float material;
		bool floor;
	};

	LARGE_INTEGER start, frequency;
	QueryPerformanceCounter(&start);
	QueryPerformanceFrequency(&frequency);
	int frame = 0;
	uint32_t lastIndex = 0;
	MSG message = {};
	for (;;) {
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) break;
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (message.message == WM_QUIT) break;
		if (frame == captureFrame) flashbang::TriggerCapture(capture, 1);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		const float time = (float)(now.QuadPart - start.QuadPart) / (float)frequency.QuadPart;
		frameConstants->viewProj = Transposed(viewProj);
		const float light[4] = {0.37f, 0.74f, -0.46f, 1.6f};
		memcpy(frameConstants->light, light, sizeof(light));

		const Item items[4] = {
			{"Floor", Scale(3.0f, 1.0f, 3.0f), {1.0f, 1.0f, 1.0f}, 1.0f, true},
			{"Cube A", Multiply(Translation(-1.4f, 0.5f, 0.0f), RotationY(time * 0.7f)), {1.0f, 0.55f, 0.35f}, 0.0f, false},
			{"Cube B", Multiply(Translation(0.0f, 0.5f, 0.6f), RotationY(-time * 0.5f)), {0.4f, 0.9f, 0.5f}, 0.0f, false},
			{"Cube C", Multiply(Translation(1.4f, 0.5f, 0.0f), RotationY(time * 0.3f)), {0.45f, 0.6f, 1.0f}, 1.0f, false},
		};

		uint32_t backIndex = 0;
		Check(g.vk.vkAcquireNextImageKHR(g.device, chain, UINT64_MAX, acquired, VK_NULL_HANDLE, &backIndex), "vkAcquireNextImageKHR");
		Check(g.vk.vkResetCommandBuffer(cb, 0), "vkResetCommandBuffer");
		VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		Check(g.vk.vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer");
		Label(g, cb, "Frame");

		Label(g, cb, "Generate pattern");
		VkImageMemoryBarrier toWrite = Barrier(pattern.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
		g.vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toWrite);
		g.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
		g.vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &sets[0], 0, nullptr);
		g.vk.vkCmdDispatch(cb, kPattern / 8, kPattern / 8, 1);
		VkImageMemoryBarrier toRead = Barrier(pattern.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		g.vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
		EndLabel(g, cb);

		Label(g, cb, "Scene");
		VkClearValue clears[2] = {};
		clears[0].color = {{0.05f, 0.07f, 0.1f, 1.0f}};
		clears[1].depthStencil = {1.0f, 0};
		VkRenderPassBeginInfo passBegin = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		passBegin.renderPass = scenePass;
		passBegin.framebuffer = sceneFramebuffer;
		passBegin.renderArea = {{0, 0}, {kWidth, kHeight}};
		passBegin.clearValueCount = 2;
		passBegin.pClearValues = clears;
		g.vk.vkCmdBeginRenderPass(cb, &passBegin, VK_SUBPASS_CONTENTS_INLINE);
		// A negative height flips y so the D3D-style projection keeps y up.
		VkViewport viewport = {0, (float)kHeight, (float)kWidth, -(float)kHeight, 0, 1};
		VkRect2D scissor = {{0, 0}, {kWidth, kHeight}};
		g.vk.vkCmdSetViewport(cb, 0, 1, &viewport);
		g.vk.vkCmdSetScissor(cb, 0, 1, &scissor);
		g.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, litPipeline);
		g.vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout, 0, 1, &sets[1], 0, nullptr);
		VkDeviceSize vertexOffset = 0;
		g.vk.vkCmdBindVertexBuffers(cb, 0, 1, &geometry.buffer, &vertexOffset);
		g.vk.vkCmdBindIndexBuffer(cb, geometry.buffer, vertexBytes, VK_INDEX_TYPE_UINT16);
		for (const Item& item : items) {
			Label(g, cb, item.name);
			ObjectConstants object = {Transposed(item.world), {item.tint[0], item.tint[1], item.tint[2], 1.0f}, {item.material, 0, 0, 0}};
			g.vk.vkCmdPushConstants(cb, scenePipelineLayout, graphics, 0, sizeof(object), &object);
			if (item.floor) g.vk.vkCmdDrawIndexed(cb, 6, 1, 36, 0, 0);
			else g.vk.vkCmdDrawIndexed(cb, 36, 1, 0, 0, 0);
			EndLabel(g, cb);
		}
		g.vk.vkCmdEndRenderPass(cb);
		EndLabel(g, cb);

		Label(g, cb, "Tonemap");
		Dependency2(g, cb, Barrier2(backBuffers[backIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));
		VkRenderingAttachmentInfo target = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
		target.imageView = backViews[backIndex];
		target.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		target.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		target.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
		rendering.renderArea = {{0, 0}, {kWidth, kHeight}};
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &target;
		g.vk.vkCmdBeginRendering(cb, &rendering);
		VkViewport full = {0, 0, (float)kWidth, (float)kHeight, 0, 1};
		g.vk.vkCmdSetViewport(cb, 0, 1, &full);
		g.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline);
		g.vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipelineLayout, 0, 1, &sets[2], 0, nullptr);
		g.vk.vkCmdDraw(cb, 3, 1, 0, 0);
		g.vk.vkCmdEndRendering(cb);
		Dependency2(g, cb, Barrier2(backBuffers[backIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0));
		EndLabel(g, cb);

		EndLabel(g, cb);
		Check(g.vk.vkEndCommandBuffer(cb), "vkEndCommandBuffer");
		const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &acquired;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cb;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &rendered;
		Check(g.vk.vkQueueSubmit(g.queue, 1, &submit, frameDone), "vkQueueSubmit");
		VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &rendered;
		present.swapchainCount = 1;
		present.pSwapchains = &chain;
		present.pImageIndices = &backIndex;
		Check(g.vk.vkQueuePresentKHR(g.queue, &present), "vkQueuePresentKHR");
		lastIndex = backIndex;
		Check(g.vk.vkWaitForFences(g.device, 1, &frameDone, VK_TRUE, UINT64_MAX), "vkWaitForFences");
		Check(g.vk.vkResetFences(g.device, 1, &frameDone), "vkResetFences");
		++frame;
		if (frames && frame >= frames) break;
	}

	g.vk.vkDeviceWaitIdle(g.device);
	if (screenshot) {
		Buffer readback = MakeBuffer(g, (VkDeviceSize)kWidth * kHeight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, nullptr);
		Once(g, [&](VkCommandBuffer copyCb) {
			VkImageMemoryBarrier toCopy = Barrier(backBuffers[lastIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, VK_ACCESS_TRANSFER_READ_BIT);
			g.vk.vkCmdPipelineBarrier(copyCb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCopy);
			VkBufferImageCopy region = {};
			region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			region.imageExtent = {kWidth, kHeight, 1};
			g.vk.vkCmdCopyImageToBuffer(copyCb, backBuffers[lastIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
			VkImageMemoryBarrier back = Barrier(backBuffers[lastIndex], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0);
			g.vk.vkCmdPipelineBarrier(copyCb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &back);
		});
		std::vector<uint8_t> bgra((const uint8_t*)readback.mapped, (const uint8_t*)readback.mapped + kWidth * kHeight * 4);
		if (surfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM || surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB)
			for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
		if (!WriteBmp(screenshot, kWidth, kHeight, bgra.data())) fprintf(stderr, "fb-sample-vulkan: cannot write the screenshot\n");
	}
	printf("fb-sample-vulkan: %d frames\n", frame);
	return 0;
}
