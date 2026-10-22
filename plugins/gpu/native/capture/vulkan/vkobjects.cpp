// The layer's object hooks: each creation calls down, then keeps the object's creation record (its structures as
// serialized when it was made, with what the API says to ignore left out) and what a capture needs of its state -
// bindings, mappings, image layouts, descriptor contents, command buffers' recordings, names.
#include "vklayer.h"

#include <cstring>

namespace fb::capture::vulkan
{
	static u64 H(const void* handle) { return (u64)(uintptr_t)handle; }
	template <class T> static u64 H(T* handle) { return (u64)(uintptr_t)handle; }

	// A new object's record: serialized, with the pNext structures it lost noted, and told to a running capture.
	template <class R> static Object* Made(Device& device, u64 handle, u32 record, ObjectKind kind, R& value) {
		Object* object = device.Add(handle, record, kind);
		value.id = object->id;
		device.Serialize(object, value);
		object->droppedNext = TakeDroppedNext();
		device.frames.Created(object);
		return object;
	}

	template <class Info> static Object* MadeFrom(Device& device, u64 handle, u32 record, ObjectKind kind, const Info& info) {
		Create<Info> value = {0, info};
		return Made(device, handle, record, kind, value);
	}

	static void Destroy(Device* device, u64 handle) {
		if (device && handle) device->Remove(handle);
	}

	// --- Memory ----------------------------------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateMemory(VkDevice handle, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* a, VkDeviceMemory* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkAllocateMemory(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		const u32 flags = info->memoryTypeIndex < device->memory.memoryTypeCount ? device->memory.memoryTypes[info->memoryTypeIndex].propertyFlags : 0;
		MemoryCreate record = {0, *info, flags};
		Object* object = Made(*device, H(*out), kMemory, ObjectKind::Memory, record);
		object->memoryType = info->memoryTypeIndex;
		object->memoryFlags = flags;
		object->size = info->allocationSize;
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkFreeMemory(VkDevice handle, VkDeviceMemory memory, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkFreeMemory(handle, memory, a);
		Destroy(device, H(memory));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkMapMemory(VkDevice handle, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkMapMemory(handle, memory, offset, size, flags, out);
		if (result != VK_SUCCESS) return result;
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		if (Object* object = device->Find(memory)) {
			object->mapped = *out;
			object->mappedOffset = offset;
			object->mappedSize = size == VK_WHOLE_SIZE ? object->size - offset : size;
		}
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkUnmapMemory(VkDevice handle, VkDeviceMemory memory) {
		Device* device = DeviceOf(handle);
		{
			std::lock_guard<std::recursive_mutex> guard(device->lock);
			if (Object* object = device->Find(memory)) object->mapped = nullptr;
		}
		device->next.vkUnmapMemory(handle, memory);
	}

	// --- Buffers and images: created with the transfer usages a capture needs, bound later -------------------------

	// The binding sits at the end of the creation record: memory id and offset, `tail` bytes from its end.
	static void Bind(Device& device, Object* object, VkDeviceMemory memory, VkDeviceSize offset, size_t tail) {
		std::lock_guard<std::recursive_mutex> guard(device.lock);
		Object* owner = device.Find(memory);
		object->memory = owner ? owner->id : 0;
		object->memoryOffset = offset;
		const size_t at = object->create.Size() - tail;
		object->create.PatchU64(at, object->memory);
		object->create.PatchU64(at + 8, offset);
		// Made during a captured frame: its record again, now bound (a replay binds an object it already made).
		device.frames.Created(object);
	}

	static constexpr size_t kBufferBindingTail = 24;   // memory, memoryOffset, size
	static constexpr size_t kImageBindingTail = 36;    // memory, memoryOffset, size, swapchain, swapchainIndex

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateBuffer(VkDevice handle, const VkBufferCreateInfo* info, const VkAllocationCallbacks* a, VkBuffer* out) {
		Device* device = DeviceOf(handle);
		VkBufferCreateInfo patched = *info;
		patched.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (patched.sharingMode != VK_SHARING_MODE_CONCURRENT) {
			patched.queueFamilyIndexCount = 0;
			patched.pQueueFamilyIndices = nullptr;
		}
		const VkResult result = device->next.vkCreateBuffer(handle, &patched, a, out);
		if (result != VK_SUCCESS) return result;
		VkMemoryRequirements needs = {};
		device->next.vkGetBufferMemoryRequirements(handle, *out, &needs);
		BufferCreate record = {0, patched, 0, 0, needs.size};
		Object* object = Made(*device, H(*out), kBuffer, ObjectKind::Buffer, record);
		object->bufferInfo = patched;
		object->bufferInfo.pNext = nullptr;
		object->bufferInfo.pQueueFamilyIndices = nullptr;
		object->requirementsSize = needs.size;
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyBuffer(VkDevice handle, VkBuffer buffer, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyBuffer(handle, buffer, a);
		Destroy(device, H(buffer));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBindBufferMemory(VkDevice handle, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkBindBufferMemory(handle, buffer, memory, offset);
		if (result == VK_SUCCESS)
			if (Object* object = device->Find(buffer)) Bind(*device, object, memory, offset, kBufferBindingTail);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBindBufferMemory2(VkDevice handle, uint32_t count, const VkBindBufferMemoryInfo* infos) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkBindBufferMemory2(handle, count, infos);
		if (result == VK_SUCCESS)
			for (u32 i = 0; i < count; ++i)
				if (Object* object = device->Find(infos[i].buffer)) Bind(*device, object, infos[i].memory, infos[i].memoryOffset, kBufferBindingTail);
		return result;
	}

	// Transfer usages only where the format can be copied with this tiling (a capture reads and restores through them).
	static VkImageUsageFlags TransferUsages(Device& device, const VkImageCreateInfo& info) {
		VkFormatProperties properties = {};
		device.instance->next.vkGetPhysicalDeviceFormatProperties(device.physical, info.format, &properties);
		const VkFormatFeatureFlags features = info.tiling == VK_IMAGE_TILING_LINEAR ? properties.linearTilingFeatures : properties.optimalTilingFeatures;
		VkImageUsageFlags usage = 0;
		if (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		// A transient attachment cannot be a transfer source.
		if (info.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) usage = 0;
		return usage;
	}

	static void TrackImage(Object* object, const VkImageCreateInfo& info) {
		object->imageInfo = info;
		object->imageInfo.pNext = nullptr;
		object->imageInfo.pQueueFamilyIndices = nullptr;
		object->layouts.assign((size_t)info.arrayLayers * info.mipLevels, info.initialLayout);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateImage(VkDevice handle, const VkImageCreateInfo* info, const VkAllocationCallbacks* a, VkImage* out) {
		Device* device = DeviceOf(handle);
		VkImageCreateInfo patched = *info;
		patched.usage |= TransferUsages(*device, *info);
		if (patched.sharingMode != VK_SHARING_MODE_CONCURRENT) {
			patched.queueFamilyIndexCount = 0;
			patched.pQueueFamilyIndices = nullptr;
		}
		const VkResult result = device->next.vkCreateImage(handle, &patched, a, out);
		if (result != VK_SUCCESS) return result;
		VkMemoryRequirements needs = {};
		device->next.vkGetImageMemoryRequirements(handle, *out, &needs);
		ImageCreate record = {0, patched, 0, 0, needs.size, 0, 0};
		Object* object = Made(*device, H(*out), kImage, ObjectKind::Image, record);
		object->requirementsSize = needs.size;
		TrackImage(object, patched);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyImage(VkDevice handle, VkImage image, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyImage(handle, image, a);
		Destroy(device, H(image));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBindImageMemory(VkDevice handle, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkBindImageMemory(handle, image, memory, offset);
		if (result == VK_SUCCESS)
			if (Object* object = device->Find(image)) Bind(*device, object, memory, offset, kImageBindingTail);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBindImageMemory2(VkDevice handle, uint32_t count, const VkBindImageMemoryInfo* infos) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkBindImageMemory2(handle, count, infos);
		if (result == VK_SUCCESS)
			for (u32 i = 0; i < count; ++i)
				if (Object* object = device->Find(infos[i].image)) Bind(*device, object, infos[i].memory, infos[i].memoryOffset, kImageBindingTail);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateImageView(VkDevice handle, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* a, VkImageView* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateImageView(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		Object* object = MadeFrom(*device, H(*out), kImageView, ObjectKind::ImageView, *info);
		Object* image = device->Find(info->image);
		object->viewImage = image ? image->id : 0;
		object->viewRange = info->subresourceRange;
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyImageView(VkDevice handle, VkImageView view, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyImageView(handle, view, a);
		Destroy(device, H(view));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateBufferView(VkDevice handle, const VkBufferViewCreateInfo* info, const VkAllocationCallbacks* a, VkBufferView* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateBufferView(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kBufferView, ObjectKind::BufferView, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyBufferView(VkDevice handle, VkBufferView view, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyBufferView(handle, view, a);
		Destroy(device, H(view));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateSampler(VkDevice handle, const VkSamplerCreateInfo* info, const VkAllocationCallbacks* a, VkSampler* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateSampler(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kSampler, ObjectKind::Sampler, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroySampler(VkDevice handle, VkSampler sampler, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroySampler(handle, sampler, a);
		Destroy(device, H(sampler));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateShaderModule(VkDevice handle, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks* a, VkShaderModule* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateShaderModule(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kShaderModule, ObjectKind::ShaderModule, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyShaderModule(VkDevice handle, VkShaderModule module, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyShaderModule(handle, module, a);
		Destroy(device, H(module));
	}

	// --- Layouts, pools and descriptor sets --------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateDescriptorSetLayout(VkDevice handle, const VkDescriptorSetLayoutCreateInfo* info, const VkAllocationCallbacks* a, VkDescriptorSetLayout* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateDescriptorSetLayout(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		// Immutable samplers count only for sampler bindings; elsewhere the pointer may be anything.
		std::vector<VkDescriptorSetLayoutBinding> bindings(info->pBindings, info->pBindings + info->bindingCount);
		for (VkDescriptorSetLayoutBinding& binding : bindings)
			if (binding.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER && binding.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) binding.pImmutableSamplers = nullptr;
		VkDescriptorSetLayoutCreateInfo clean = *info;
		clean.pBindings = bindings.data();
		MadeFrom(*device, H(*out), kDescriptorSetLayout, ObjectKind::DescriptorSetLayout, clean);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyDescriptorSetLayout(VkDevice handle, VkDescriptorSetLayout layout, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyDescriptorSetLayout(handle, layout, a);
		Destroy(device, H(layout));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreatePipelineLayout(VkDevice handle, const VkPipelineLayoutCreateInfo* info, const VkAllocationCallbacks* a, VkPipelineLayout* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreatePipelineLayout(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kPipelineLayout, ObjectKind::PipelineLayout, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyPipelineLayout(VkDevice handle, VkPipelineLayout layout, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyPipelineLayout(handle, layout, a);
		Destroy(device, H(layout));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateDescriptorPool(VkDevice handle, const VkDescriptorPoolCreateInfo* info, const VkAllocationCallbacks* a, VkDescriptorPool* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateDescriptorPool(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kDescriptorPool, ObjectKind::DescriptorPool, *info);
		return result;
	}

	// A pool's sets go with it (destroyed or reset).
	static void DropSets(Device& device, u64 pool) {
		std::lock_guard<std::recursive_mutex> guard(device.lock);
		std::vector<u64> handles;
		for (auto& object : device.objects)
			if (object->alive && object->kind == ObjectKind::DescriptorSet && object->pool == pool) handles.push_back(object->handle);
		for (u64 set : handles) device.Remove(set);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyDescriptorPool(VkDevice handle, VkDescriptorPool pool, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyDescriptorPool(handle, pool, a);
		if (Object* object = device->Find(pool)) DropSets(*device, object->id);
		Destroy(device, H(pool));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkResetDescriptorPool(VkDevice handle, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkResetDescriptorPool(handle, pool, flags);
		if (Object* object = device->Find(pool)) DropSets(*device, object->id);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateDescriptorSets(VkDevice handle, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkAllocateDescriptorSets(handle, info, out);
		if (result != VK_SUCCESS) return result;
		const VkDescriptorSetVariableDescriptorCountAllocateInfo* variable = nullptr;
		for (const VkBaseInStructure* next = (const VkBaseInStructure*)info->pNext; next; next = next->pNext)
			if (next->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO) variable = (const VkDescriptorSetVariableDescriptorCountAllocateInfo*)next;
		Object* pool = device->Find(info->descriptorPool);
		for (u32 i = 0; i < info->descriptorSetCount; ++i) {
			Object* layout = device->Find(info->pSetLayouts[i]);
			DescriptorSetCreate record = {0, pool ? pool->id : 0, layout ? layout->id : 0, variable && i < variable->descriptorSetCount ? variable->pDescriptorCounts[i] : 0};
			Object* object = Made(*device, H(out[i]), kDescriptorSet, ObjectKind::DescriptorSet, record);
			object->pool = record.pool;
			object->setLayout = record.layout;
		}
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkFreeDescriptorSets(VkDevice handle, VkDescriptorPool pool, uint32_t count, const VkDescriptorSet* sets) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkFreeDescriptorSets(handle, pool, count, sets);
		for (u32 i = 0; i < count; ++i) Destroy(device, H(sets[i]));
		return result;
	}

	static bool ImageDescriptor(VkDescriptorType type) {
		return type == VK_DESCRIPTOR_TYPE_SAMPLER || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
			type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	}

	static bool BufferDescriptor(VkDescriptorType type) {
		return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
			type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	}

	static bool TexelDescriptor(VkDescriptorType type) {
		return type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
	}

	// The sets' shadows: what each element holds, so a capture can write every set's contents at its start.
	static void Shadow(Device& device, u32 writeCount, const VkWriteDescriptorSet* writes, u32 copyCount, const VkCopyDescriptorSet* copies) {
		std::lock_guard<std::recursive_mutex> guard(device.lock);
		for (u32 w = 0; w < writeCount; ++w) {
			const VkWriteDescriptorSet& write = writes[w];
			Object* set = device.Find(write.dstSet);
			if (!set) continue;
			if (write.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
				device.frames.Warn("inline uniform blocks are not captured; their contents read as zero on replay");
				continue;
			}
			for (u32 i = 0; i < write.descriptorCount; ++i) {
				DescriptorEntry entry = {};
				entry.type = write.descriptorType;
				if (ImageDescriptor(write.descriptorType)) entry.image = write.pImageInfo[i];
				else if (BufferDescriptor(write.descriptorType)) entry.buffer = write.pBufferInfo[i];
				else if (TexelDescriptor(write.descriptorType)) entry.texel = write.pTexelBufferView[i];
				else continue;
				set->descriptors[((u64)write.dstBinding << 32) | (write.dstArrayElement + i)] = entry;
			}
		}
		for (u32 c = 0; c < copyCount; ++c) {
			const VkCopyDescriptorSet& copy = copies[c];
			Object* from = device.Find(copy.srcSet);
			Object* to = device.Find(copy.dstSet);
			if (!from || !to) continue;
			for (u32 i = 0; i < copy.descriptorCount; ++i) {
				auto found = from->descriptors.find(((u64)copy.srcBinding << 32) | (copy.srcArrayElement + i));
				if (found != from->descriptors.end()) to->descriptors[((u64)copy.dstBinding << 32) | (copy.dstArrayElement + i)] = found->second;
			}
		}
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkUpdateDescriptorSets(VkDevice handle, uint32_t writeCount, const VkWriteDescriptorSet* writes, uint32_t copyCount, const VkCopyDescriptorSet* copies) {
		Device* device = DeviceOf(handle);
		device->next.vkUpdateDescriptorSets(handle, writeCount, writes, copyCount, copies);
		Shadow(*device, writeCount, writes, copyCount, copies);
		if (device->frames.Active()) {
			ByteWriter payload;
			DescriptorUpdate update = {writeCount, writes, copyCount, copies};
			Ser s(payload, device);
			Serialize(s, update);
			TakeDroppedNext();
			device->frames.DescriptorsUpdated(payload);
		}
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkUpdateDescriptorSetWithTemplate(VkDevice handle, VkDescriptorSet set, VkDescriptorUpdateTemplate update, const void* data) {
		Device* device = DeviceOf(handle);
		device->next.vkUpdateDescriptorSetWithTemplate(handle, set, update, data);
		device->frames.Warn("descriptor update templates are not captured yet; the sets they update are empty on replay");
	}

	// --- Render passes, framebuffers, pipelines ----------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateRenderPass(VkDevice handle, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* a, VkRenderPass* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateRenderPass(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		RenderPassCreate record = {0, 1, *info, {}};
		Object* object = Made(*device, H(*out), kRenderPass, ObjectKind::RenderPass, record);
		for (u32 i = 0; i < info->attachmentCount; ++i) object->finalLayouts.push_back(info->pAttachments[i].finalLayout);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateRenderPass2(VkDevice handle, const VkRenderPassCreateInfo2* info, const VkAllocationCallbacks* a, VkRenderPass* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateRenderPass2(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		RenderPassCreate record = {0, 2, {}, *info};
		Object* object = Made(*device, H(*out), kRenderPass, ObjectKind::RenderPass, record);
		for (u32 i = 0; i < info->attachmentCount; ++i) object->finalLayouts.push_back(info->pAttachments[i].finalLayout);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyRenderPass(VkDevice handle, VkRenderPass pass, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyRenderPass(handle, pass, a);
		Destroy(device, H(pass));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateFramebuffer(VkDevice handle, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks* a, VkFramebuffer* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateFramebuffer(handle, info, a, out);
		if (result != VK_SUCCESS) return result;
		VkFramebufferCreateInfo clean = *info;
		const bool imageless = (info->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) != 0;
		if (imageless) clean.pAttachments = nullptr;
		Object* object = MadeFrom(*device, H(*out), kFramebuffer, ObjectKind::Framebuffer, clean);
		if (!imageless)
			for (u32 i = 0; i < info->attachmentCount; ++i) {
				Object* view = device->Find(info->pAttachments[i]);
				object->attachments.push_back(view ? view->id : 0);
			}
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyFramebuffer(VkDevice handle, VkFramebuffer framebuffer, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyFramebuffer(handle, framebuffer, a);
		Destroy(device, H(framebuffer));
	}

	static bool HasDynamic(const VkPipelineDynamicStateCreateInfo* dynamic, VkDynamicState state) {
		if (!dynamic) return false;
		for (u32 i = 0; i < dynamic->dynamicStateCount; ++i)
			if (dynamic->pDynamicStates[i] == state) return true;
		return false;
	}

	// A graphics pipeline's description without what the API ignores (which may be anything): tessellation state
	// without tessellation shaders, viewports and scissors that are dynamic.
	struct CleanPipeline
	{
		VkGraphicsPipelineCreateInfo info;
		VkPipelineViewportStateCreateInfo viewport;
	};

	static void Clean(const VkGraphicsPipelineCreateInfo& in, CleanPipeline& out) {
		out.info = in;
		bool tessellation = false;
		for (u32 i = 0; i < in.stageCount; ++i)
			if (in.pStages[i].stage & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) tessellation = true;
		if (!tessellation) out.info.pTessellationState = nullptr;
		if (in.pViewportState) {
			out.viewport = *in.pViewportState;
			if (HasDynamic(in.pDynamicState, VK_DYNAMIC_STATE_VIEWPORT) || HasDynamic(in.pDynamicState, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)) out.viewport.pViewports = nullptr;
			if (HasDynamic(in.pDynamicState, VK_DYNAMIC_STATE_SCISSOR) || HasDynamic(in.pDynamicState, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT)) out.viewport.pScissors = nullptr;
			out.info.pViewportState = &out.viewport;
		}
		if (!(in.flags & VK_PIPELINE_CREATE_DERIVATIVE_BIT)) out.info.basePipelineHandle = VK_NULL_HANDLE;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateGraphicsPipelines(VkDevice handle, VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo* infos,
		const VkAllocationCallbacks* a, VkPipeline* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateGraphicsPipelines(handle, cache, count, infos, a, out);
		for (u32 i = 0; i < count; ++i) {
			if (!out[i]) continue;
			CleanPipeline clean;
			Clean(infos[i], clean);
			MadeFrom(*device, H(out[i]), kGraphicsPipeline, ObjectKind::Pipeline, clean.info);
		}
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateComputePipelines(VkDevice handle, VkPipelineCache cache, uint32_t count, const VkComputePipelineCreateInfo* infos,
		const VkAllocationCallbacks* a, VkPipeline* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateComputePipelines(handle, cache, count, infos, a, out);
		for (u32 i = 0; i < count; ++i) {
			if (!out[i]) continue;
			VkComputePipelineCreateInfo clean = infos[i];
			if (!(clean.flags & VK_PIPELINE_CREATE_DERIVATIVE_BIT)) clean.basePipelineHandle = VK_NULL_HANDLE;
			MadeFrom(*device, H(out[i]), kComputePipeline, ObjectKind::Pipeline, clean);
		}
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyPipeline(VkDevice handle, VkPipeline pipeline, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyPipeline(handle, pipeline, a);
		Destroy(device, H(pipeline));
	}

	// --- Command pools and buffers -----------------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateCommandPool(VkDevice handle, const VkCommandPoolCreateInfo* info, const VkAllocationCallbacks* a, VkCommandPool* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateCommandPool(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kCommandPool, ObjectKind::CommandPool, *info);
		return result;
	}

	static void ForPoolBuffers(Device& device, u64 pool, void (*act)(Device&, Object*)) {
		std::lock_guard<std::recursive_mutex> guard(device.lock);
		std::vector<Object*> buffers;
		for (auto& object : device.objects)
			if (object->alive && object->kind == ObjectKind::CommandBuffer && object->pool == pool) buffers.push_back(object.get());
		for (Object* buffer : buffers) act(device, buffer);
	}

	static void ResetRecording(Device&, Object* buffer) {
		if (!buffer->recording) return;
		*buffer->recording = Recording();
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyCommandPool(VkDevice handle, VkCommandPool pool, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyCommandPool(handle, pool, a);
		if (Object* object = device->Find(pool)) ForPoolBuffers(*device, object->id, [](Device& d, Object* buffer) { d.Remove(buffer->handle); });
		Destroy(device, H(pool));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkResetCommandPool(VkDevice handle, VkCommandPool pool, VkCommandPoolResetFlags flags) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkResetCommandPool(handle, pool, flags);
		if (Object* object = device->Find(pool)) ForPoolBuffers(*device, object->id, ResetRecording);
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkAllocateCommandBuffers(VkDevice handle, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkAllocateCommandBuffers(handle, info, out);
		if (result != VK_SUCCESS) return result;
		Object* pool = device->Find(info->commandPool);
		for (u32 i = 0; i < info->commandBufferCount; ++i) {
			CommandBufferCreate record = {0, pool ? pool->id : 0, info->level};
			Object* object = Made(*device, H(out[i]), kCommandBuffer, ObjectKind::CommandBuffer, record);
			object->pool = record.pool;
			object->level = info->level;
			object->recording = std::make_unique<Recording>();
		}
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkFreeCommandBuffers(VkDevice handle, VkCommandPool pool, uint32_t count, const VkCommandBuffer* buffers) {
		Device* device = DeviceOf(handle);
		device->next.vkFreeCommandBuffers(handle, pool, count, buffers);
		for (u32 i = 0; i < count; ++i) Destroy(device, H(buffers[i]));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkBeginCommandBuffer(VkCommandBuffer handle, const VkCommandBufferBeginInfo* info) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkBeginCommandBuffer(handle, info);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		if (Object* object = device->Find(handle)) {
			ResetRecording(*device, object);
			object->recording->valid = true;
		}
		return result;
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkEndCommandBuffer(VkCommandBuffer handle) {
		return DeviceOf(handle)->next.vkEndCommandBuffer(handle);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkResetCommandBuffer(VkCommandBuffer handle, VkCommandBufferResetFlags flags) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkResetCommandBuffer(handle, flags);
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		if (Object* object = device->Find(handle)) ResetRecording(*device, object);
		return result;
	}

	// --- Synchronization and queries ---------------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateFence(VkDevice handle, const VkFenceCreateInfo* info, const VkAllocationCallbacks* a, VkFence* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateFence(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kFence, ObjectKind::Fence, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyFence(VkDevice handle, VkFence fence, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyFence(handle, fence, a);
		Destroy(device, H(fence));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateSemaphore(VkDevice handle, const VkSemaphoreCreateInfo* info, const VkAllocationCallbacks* a, VkSemaphore* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateSemaphore(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kSemaphore, ObjectKind::Semaphore, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroySemaphore(VkDevice handle, VkSemaphore semaphore, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroySemaphore(handle, semaphore, a);
		Destroy(device, H(semaphore));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateEvent(VkDevice handle, const VkEventCreateInfo* info, const VkAllocationCallbacks* a, VkEvent* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateEvent(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kEvent, ObjectKind::Event, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyEvent(VkDevice handle, VkEvent event, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyEvent(handle, event, a);
		Destroy(device, H(event));
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateQueryPool(VkDevice handle, const VkQueryPoolCreateInfo* info, const VkAllocationCallbacks* a, VkQueryPool* out) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkCreateQueryPool(handle, info, a, out);
		if (result == VK_SUCCESS) MadeFrom(*device, H(*out), kQueryPool, ObjectKind::QueryPool, *info);
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroyQueryPool(VkDevice handle, VkQueryPool pool, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroyQueryPool(handle, pool, a);
		Destroy(device, H(pool));
	}

	// --- Queues and swap chains --------------------------------------------------------------------------------------

	static void NoteQueue(Device& device, VkQueue queue, u32 family, u32 index) {
		if (!queue || device.Find(queue)) return;
		const u32 flags = family < device.families.size() ? device.families[family].queueFlags : 0;
		QueueCreate record = {0, family, index, flags};
		Object* object = Made(device, H(queue), kQueue, ObjectKind::Queue, record);
		object->family = family;
		object->index = index;
		object->familyFlags = flags;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkGetDeviceQueue(VkDevice handle, uint32_t family, uint32_t index, VkQueue* out) {
		Device* device = DeviceOf(handle);
		device->next.vkGetDeviceQueue(handle, family, index, out);
		NoteQueue(*device, *out, family, index);
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkGetDeviceQueue2(VkDevice handle, const VkDeviceQueueInfo2* info, VkQueue* out) {
		Device* device = DeviceOf(handle);
		device->next.vkGetDeviceQueue2(handle, info, out);
		NoteQueue(*device, *out, info->queueFamilyIndex, info->queueIndex);
	}

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkCreateSwapchainKHR(VkDevice handle, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks* a, VkSwapchainKHR* out) {
		Device* device = DeviceOf(handle);
		VkSwapchainCreateInfoKHR patched = *info;
		// The presented image is read for the thumbnail (and restored on replay) when the surface lets it be copied.
		VkSurfaceCapabilitiesKHR caps = {};
		if (device->instance->next.vkGetPhysicalDeviceSurfaceCapabilitiesKHR &&
			device->instance->next.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical, info->surface, &caps) == VK_SUCCESS)
			patched.imageUsage |= caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		if (patched.imageSharingMode != VK_SHARING_MODE_CONCURRENT) {
			patched.queueFamilyIndexCount = 0;
			patched.pQueueFamilyIndices = nullptr;
		}
		const VkResult result = device->next.vkCreateSwapchainKHR(handle, &patched, a, out);
		if (result != VK_SUCCESS) return result;
		SwapchainCreate record = {0, patched, 0, nullptr};
		Object* object = Made(*device, H(*out), kSwapchain, ObjectKind::Swapchain, record);
		object->swapchainInfo = patched;
		object->swapchainInfo.pNext = nullptr;
		return result;
	}

	static VKAPI_ATTR void VKAPI_CALL hook_vkDestroySwapchainKHR(VkDevice handle, VkSwapchainKHR chain, const VkAllocationCallbacks* a) {
		Device* device = DeviceOf(handle);
		device->next.vkDestroySwapchainKHR(handle, chain, a);
		if (Object* object = device->Find(chain)) {
			std::vector<u64> images = object->images;
			for (u64 imageId : images)
				if (Object* image = device->FindId(imageId)) device->Remove(image->handle);
		}
		Destroy(device, H(chain));
	}

	// A swap chain's images: images with the swap chain's description (a replay makes them as ordinary images).
	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkGetSwapchainImagesKHR(VkDevice handle, VkSwapchainKHR chain, uint32_t* count, VkImage* images) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkGetSwapchainImagesKHR(handle, chain, count, images);
		if (!images || (result != VK_SUCCESS && result != VK_INCOMPLETE)) return result;
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		Object* owner = device->Find(chain);
		if (!owner) return result;
		const VkSwapchainCreateInfoKHR& c = owner->swapchainInfo;
		for (u32 i = 0; i < *count; ++i) {
			if (device->Find(images[i])) continue;
			VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = c.imageFormat;
			info.extent = {c.imageExtent.width, c.imageExtent.height, 1};
			info.mipLevels = 1;
			info.arrayLayers = c.imageArrayLayers;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = c.imageUsage;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ImageCreate record = {0, info, 0, 0, 0, owner->id, i};
			Object* image = Made(*device, H(images[i]), kImage, ObjectKind::Image, record);
			image->swapchain = owner->id;
			image->swapchainIndex = i;
			TrackImage(image, info);
			owner->images.push_back(image->id);
		}
		SwapchainCreate record = {owner->id, owner->swapchainInfo, (u32)owner->images.size(), owner->images.data()};
		device->Serialize(owner, record);
		TakeDroppedNext();
		return result;
	}

	// --- Names ---------------------------------------------------------------------------------------------------------

	static VKAPI_ATTR VkResult VKAPI_CALL hook_vkSetDebugUtilsObjectNameEXT(VkDevice handle, const VkDebugUtilsObjectNameInfoEXT* info) {
		Device* device = DeviceOf(handle);
		const VkResult result = device->next.vkSetDebugUtilsObjectNameEXT ? device->next.vkSetDebugUtilsObjectNameEXT(handle, info) : VK_SUCCESS;
		std::lock_guard<std::recursive_mutex> guard(device->lock);
		if (Object* object = device->Find(info->objectHandle)) {
			object->name = info->pObjectName ? info->pObjectName : "";
			device->frames.Named(object);
		}
		return result;
	}

	PFN_vkVoidFunction ObjectHook(const char* name) {
		struct Entry
		{
			const char* name;
			PFN_vkVoidFunction function;
		};
#define FB_HOOK(name) {#name, (PFN_vkVoidFunction)&hook_##name},
		static const Entry entries[] = {
			FB_HOOK(vkGetDeviceQueue) FB_HOOK(vkGetDeviceQueue2) FB_HOOK(vkAllocateMemory) FB_HOOK(vkFreeMemory) FB_HOOK(vkMapMemory)
			FB_HOOK(vkUnmapMemory) FB_HOOK(vkCreateBuffer) FB_HOOK(vkDestroyBuffer) FB_HOOK(vkBindBufferMemory) FB_HOOK(vkBindBufferMemory2)
			FB_HOOK(vkCreateImage) FB_HOOK(vkDestroyImage) FB_HOOK(vkBindImageMemory) FB_HOOK(vkBindImageMemory2) FB_HOOK(vkCreateImageView)
			FB_HOOK(vkDestroyImageView) FB_HOOK(vkCreateBufferView) FB_HOOK(vkDestroyBufferView) FB_HOOK(vkCreateSampler) FB_HOOK(vkDestroySampler)
			FB_HOOK(vkCreateShaderModule) FB_HOOK(vkDestroyShaderModule) FB_HOOK(vkCreateDescriptorSetLayout) FB_HOOK(vkDestroyDescriptorSetLayout)
			FB_HOOK(vkCreatePipelineLayout) FB_HOOK(vkDestroyPipelineLayout) FB_HOOK(vkCreateDescriptorPool) FB_HOOK(vkDestroyDescriptorPool)
			FB_HOOK(vkResetDescriptorPool) FB_HOOK(vkAllocateDescriptorSets) FB_HOOK(vkFreeDescriptorSets) FB_HOOK(vkUpdateDescriptorSets)
			FB_HOOK(vkUpdateDescriptorSetWithTemplate) FB_HOOK(vkCreateRenderPass) FB_HOOK(vkCreateRenderPass2) FB_HOOK(vkDestroyRenderPass)
			FB_HOOK(vkCreateFramebuffer) FB_HOOK(vkDestroyFramebuffer) FB_HOOK(vkCreateGraphicsPipelines) FB_HOOK(vkCreateComputePipelines)
			FB_HOOK(vkDestroyPipeline) FB_HOOK(vkCreateCommandPool) FB_HOOK(vkDestroyCommandPool) FB_HOOK(vkResetCommandPool)
			FB_HOOK(vkAllocateCommandBuffers) FB_HOOK(vkFreeCommandBuffers) FB_HOOK(vkBeginCommandBuffer) FB_HOOK(vkEndCommandBuffer)
			FB_HOOK(vkResetCommandBuffer) FB_HOOK(vkCreateFence) FB_HOOK(vkDestroyFence) FB_HOOK(vkCreateSemaphore) FB_HOOK(vkDestroySemaphore)
			FB_HOOK(vkCreateEvent) FB_HOOK(vkDestroyEvent) FB_HOOK(vkCreateQueryPool) FB_HOOK(vkDestroyQueryPool) FB_HOOK(vkCreateSwapchainKHR)
			FB_HOOK(vkDestroySwapchainKHR) FB_HOOK(vkGetSwapchainImagesKHR) FB_HOOK(vkSetDebugUtilsObjectNameEXT)
		};
#undef FB_HOOK
		for (const Entry& entry : entries)
			if (!strcmp(entry.name, name)) return entry.function;
		return nullptr;
	}
}
