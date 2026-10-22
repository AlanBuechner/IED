// One Vulkan device's frame capture (vklayer.h): the setup and initial contents at the frame's start, the frame's
// submissions, CPU writes and objects while it runs, and the thumbnail, metadata and file at its end.
#include "vklayer.h"

#include "formats.h"
#include "png.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fb::capture::vulkan
{
	// Readback memory per batch of initial-contents copies (a larger resource gets a batch of its own size).
	static constexpr u64 kCopyBatch = 256ull * 1024 * 1024;
	// Mapped memory is compared with its shadow in blocks of this size.
	static constexpr size_t kDiffBlock = 256;
	static constexpr u64 kWaitNs = 60ull * 1000 * 1000 * 1000;

	FrameCapture::~FrameCapture() {
		if (fence_) device_.next.vkDestroyFence(device_.handle, fence_, nullptr);
		if (copied_) device_.next.vkDestroySemaphore(device_.handle, copied_, nullptr);
		if (pool_) device_.next.vkDestroyCommandPool(device_.handle, pool_, nullptr);
	}

	template <class R> void FrameCapture::Record(u32 type, R& record) {
		ByteWriter payload;
		Ser s(payload, &device_);
		Serialize(s, record);
		TakeDroppedNext();
		RecordHeader header = {type, 0, payload.Size()};
		stream_.Put(header);
		stream_.Bytes(payload.Data(), payload.Size());
	}

	static void RawRecord(ByteWriter& stream, u32 type, const ByteWriter& payload) {
		RecordHeader header = {type, 0, payload.Size()};
		stream.Put(header);
		stream.Bytes(payload.Data(), payload.Size());
	}

	void FrameCapture::Warn(const std::string& text) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		for (const std::string& known : warnings_)
			if (known == text) return;
		warnings_.push_back(text);
		Log("capture warning: %s", text.c_str());
	}

	void FrameCapture::Unsupported(const Recording& recording) {
		for (const std::string& name : recording.unsupported) Warn(name + " is not captured yet; replay skips it");
		for (u32 type : recording.droppedNext) Warn("a command's pNext structure (sType " + std::to_string(type) + ") is not captured; replay leaves it out");
	}

	// --- The layer's own command buffer --------------------------------------------------------------------------

	bool FrameCapture::MakeObjects(VkQueue queue) {
		Object* owner = device_.Find(queue);
		const u32 family = owner ? owner->family : 0;
		queue_ = queue;
		if (pool_ && family == family_) return true;
		const VkDevice d = device_.handle;
		if (pool_) device_.next.vkDestroyCommandPool(d, pool_, nullptr);
		pool_ = VK_NULL_HANDLE;
		VkCommandPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = family;
		if (device_.next.vkCreateCommandPool(d, &poolInfo, nullptr, &pool_) != VK_SUCCESS) return false;
		VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = pool_;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		if (device_.next.vkAllocateCommandBuffers(d, &allocate, &cb_) != VK_SUCCESS) return false;
		// A dispatchable object the layer makes carries the loader's dispatch pointer, as the program's do.
		if (device_.setLoaderData) device_.setLoaderData(d, cb_);
		if (!fence_) {
			VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			if (device_.next.vkCreateFence(d, &fenceInfo, nullptr, &fence_) != VK_SUCCESS) return false;
			VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			if (device_.next.vkCreateSemaphore(d, &semaphoreInfo, nullptr, &copied_) != VK_SUCCESS) return false;
		}
		family_ = family;
		return true;
	}

	VkCommandBuffer FrameCapture::BeginOwn() {
		device_.next.vkResetCommandBuffer(cb_, 0);
		VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		device_.next.vkBeginCommandBuffer(cb_, &begin);
		return cb_;
	}

	bool FrameCapture::SubmitOwn(VkCommandBuffer cb, const std::vector<VkSemaphore>& waits, VkSemaphore signal) {
		device_.next.vkEndCommandBuffer(cb);
		std::vector<VkPipelineStageFlags> stages(waits.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.waitSemaphoreCount = (u32)waits.size();
		submit.pWaitSemaphores = waits.data();
		submit.pWaitDstStageMask = stages.data();
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cb;
		submit.signalSemaphoreCount = signal ? 1 : 0;
		submit.pSignalSemaphores = &signal;
		if (device_.next.vkQueueSubmit(queue_, 1, &submit, fence_) != VK_SUCCESS) return false;
		const bool done = device_.next.vkWaitForFences(device_.handle, 1, &fence_, VK_TRUE, kWaitNs) == VK_SUCCESS;
		device_.next.vkResetFences(device_.handle, 1, &fence_);
		return done;
	}

	// Host memory the layer reads GPU copies from.
	struct Readback
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		u8* mapped = nullptr;
	};

	static bool MakeReadback(Device& device, u64 size, Readback& out) {
		const VkDevice d = device.handle;
		VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = size;
		info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (device.next.vkCreateBuffer(d, &info, nullptr, &out.buffer) != VK_SUCCESS) return false;
		VkMemoryRequirements needs = {};
		device.next.vkGetBufferMemoryRequirements(d, out.buffer, &needs);
		u32 type = UINT32_MAX;
		const VkMemoryPropertyFlags wanted[2] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
		for (VkMemoryPropertyFlags flags : wanted)
			for (u32 i = 0; i < device.memory.memoryTypeCount && type == UINT32_MAX; ++i)
				if ((needs.memoryTypeBits & (1u << i)) && (device.memory.memoryTypes[i].propertyFlags & flags) == flags) type = i;
		if (type == UINT32_MAX) return false;
		VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocate.allocationSize = needs.size;
		allocate.memoryTypeIndex = type;
		if (device.next.vkAllocateMemory(d, &allocate, nullptr, &out.memory) != VK_SUCCESS) return false;
		if (device.next.vkBindBufferMemory(d, out.buffer, out.memory, 0) != VK_SUCCESS) return false;
		return device.next.vkMapMemory(d, out.memory, 0, VK_WHOLE_SIZE, 0, (void**)&out.mapped) == VK_SUCCESS;
	}

	static void FreeReadback(Device& device, Readback& readback) {
		if (readback.buffer) device.next.vkDestroyBuffer(device.handle, readback.buffer, nullptr);
		if (readback.memory) device.next.vkFreeMemory(device.handle, readback.memory, nullptr);
		readback = Readback();
	}

	static VkImageMemoryBarrier ImageBarrier(VkImage image, VkImageAspectFlags aspects, u32 mip, u32 layer, VkImageLayout from, VkImageLayout to) {
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

	// --- Frame boundaries ------------------------------------------------------------------------------------------

	void FrameCapture::AfterPresent(VkQueue queue) {
		const u64 frame = Session::Get().NextFrame();
		if (!Session::Get().TakeTrigger()) return;
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		frame_ = frame + 1;
		if (!Begin(queue)) Session::Get().Failed("the Vulkan capture could not start (see the program's debug output)");
	}

	void FrameCapture::BeforePresent(VkQueue queue, const VkPresentInfoKHR& info, std::vector<VkSemaphore>& waits) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		CaptureSummary summary;
		Thumbnail(queue, info, waits, summary);
		Object* chain = info.swapchainCount ? device_.Find(info.pSwapchains[0]) : nullptr;
		End(queue, chain ? chain->id : 0, info.swapchainCount ? info.pImageIndices[0] : 0, summary);
	}

	void FrameCapture::ManualStart(u64 frame) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (active_) return;
		// The first queue the program got that can do graphics or compute.
		VkQueue queue = VK_NULL_HANDLE;
		for (auto& object : device_.objects)
			if (object->alive && object->kind == ObjectKind::Queue && (object->familyFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
				queue = (VkQueue)(uintptr_t)object->handle;
				break;
			}
		if (!queue) return;
		frame_ = frame;
		if (!Begin(queue)) Session::Get().Failed("the Vulkan capture could not start (see the program's debug output)");
	}

	void FrameCapture::ManualEnd() {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		CaptureSummary summary;
		End(queue_, 0, 0, summary);
	}

	bool FrameCapture::Begin(VkQueue queue) {
		if (!MakeObjects(queue)) {
			Log("cannot make the capture's own command buffer");
			return false;
		}
		device_.Compact();
		base_ = Session::Get().NewCaptureBase(frame_);
		std::string error;
		if (!file_.Open(base_ + L".flash", Api::Vulkan, error)) {
			Session::Get().Failed(error);
			return false;
		}
		warnings_.clear();
		stream_.Clear();
		shadows_.clear();
		events_ = 0;
		device_.next.vkDeviceWaitIdle(device_.handle);
		WriteSetup();
		WriteInitialContents();
		file_.Record(kRecordFrameBegin, nullptr, 0);
		active_ = true;
		Session::Get().SetCapturing(true);
		Log("capturing Vulkan frame %llu", (unsigned long long)frame_);
		return true;
	}

	// --- The frame's start: objects, descriptor sets, layouts ------------------------------------------------------

	void FrameCapture::WriteSetup() {
		file_.Record(kDevice, device_.info);
		for (auto& object : device_.objects) {
			if (!object->alive) continue;
			file_.Record(object->record, object->create);
			if (!object->name.empty()) {
				ObjectName name = {object->id, object->name};
				ByteWriter payload;
				Ser s(payload, &device_);
				Serialize(s, name);
				file_.Record(kName, payload);
			}
			for (u32 type : object->droppedNext)
				warnings_.push_back(std::string(ObjectKindName(object->kind)) + " " + std::to_string(object->id) + ": a pNext structure (sType " + std::to_string(type) +
					") is not captured; replay leaves it out");
		}
		// Each set's contents, one write per element.
		for (auto& object : device_.objects) {
			if (!object->alive || object->kind != ObjectKind::DescriptorSet || object->descriptors.empty()) continue;
			const size_t n = object->descriptors.size();
			std::vector<VkWriteDescriptorSet> writes;
			std::vector<VkDescriptorImageInfo> images;
			std::vector<VkDescriptorBufferInfo> buffers;
			std::vector<VkBufferView> texels;
			writes.reserve(n);
			images.reserve(n);
			buffers.reserve(n);
			texels.reserve(n);
			for (const auto& entry : object->descriptors) {
				VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
				write.dstSet = (VkDescriptorSet)(uintptr_t)object->handle;
				write.dstBinding = (u32)(entry.first >> 32);
				write.dstArrayElement = (u32)entry.first;
				write.descriptorCount = 1;
				write.descriptorType = entry.second.type;
				images.push_back(entry.second.image);
				buffers.push_back(entry.second.buffer);
				texels.push_back(entry.second.texel);
				write.pImageInfo = &images.back();
				write.pBufferInfo = &buffers.back();
				write.pTexelBufferView = &texels.back();
				writes.push_back(write);
			}
			DescriptorUpdate update = {(u32)writes.size(), writes.data(), 0, nullptr};
			ByteWriter payload;
			Ser s(payload, &device_);
			Serialize(s, update);
			file_.Record(kDescriptorUpdate, payload);
		}
		for (auto& object : device_.objects) {
			if (!object->alive || object->kind != ObjectKind::Image || object->layouts.empty()) continue;
			ImageLayout layout = {object->id, (u32)object->layouts.size(), object->layouts.data()};
			ByteWriter payload;
			Ser s(payload, &device_);
			Serialize(s, layout);
			file_.Record(kImageLayout, payload);
		}
		TakeDroppedNext();
	}

	// --- The frame's start: initial contents -------------------------------------------------------------------------

	// One GPU copy into readback memory: a whole buffer, or one subresource of one aspect of an image.
	struct Copy
	{
		Object* object;
		VkImageAspectFlagBits aspect;   // 0 for a buffer
		u32 mip;
		u32 layer;
		VkExtent3D extent;
		u64 bytes;
		u64 offset;                     // in its batch's readback buffer
	};

	static u64 AlignUp(u64 value, u64 alignment) { return (value + alignment - 1) / alignment * alignment; }

	void FrameCapture::WriteInitialContents() {
		const VkDevice d = device_.handle;
		auto hostVisible = [&](u64 memoryId) {
			Object* memory = device_.FindId(memoryId);
			return memory && (memory->memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		};

		// Host-visible memory is read where it is (buffers and linear images in it come along).
		for (auto& object : device_.objects) {
			if (!object->alive || object->kind != ObjectKind::Memory || !(object->memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
			const VkDeviceMemory memory = (VkDeviceMemory)(uintptr_t)object->handle;
			u8* data = nullptr;
			u64 offset = 0, size = object->size;
			bool ours = false;
			if (object->mapped) {
				data = (u8*)object->mapped;
				offset = object->mappedOffset;
				size = object->mappedSize;
				if (offset != 0 || size != object->size)
					warnings_.push_back("memory " + std::to_string(object->id) + " is mapped in part; only that part's contents are captured");
			}
			else if (device_.next.vkMapMemory(d, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data) == VK_SUCCESS)
				ours = true;
			if (!data) continue;
			if (!(object->memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, memory, ours ? 0 : offset, VK_WHOLE_SIZE};
				device_.next.vkInvalidateMappedMemoryRanges(d, 1, &range);
			}
			MemoryContents contents = {object->id, offset, data, size};
			ByteWriter payload;
			Ser s(payload, &device_);
			Serialize(s, contents);
			file_.Record(kMemoryContents, payload);
			shadows_.push_back({object->id, offset, std::vector<u8>(data, data + size)});
			if (ours) device_.next.vkUnmapMemory(d, memory);
		}

		// Buffers and images in device memory are copied out.
		std::vector<Copy> copies;
		for (auto& object : device_.objects) {
			if (!object->alive) continue;
			if (object->kind == ObjectKind::Buffer) {
				if (!object->memory || hostVisible(object->memory) || !object->bufferInfo.size) continue;
				if (object->bufferInfo.flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
					warnings_.push_back("sparse buffers are not captured; they start empty on replay");
					continue;
				}
				copies.push_back({object.get(), (VkImageAspectFlagBits)0, 0, 0, {}, object->bufferInfo.size, 0});
			}
			else if (object->kind == ObjectKind::Image) {
				const VkImageCreateInfo& info = object->imageInfo;
				if (object->swapchain || (object->memory && hostVisible(object->memory)) || !object->memory) continue;
				const bool defined = std::any_of(object->layouts.begin(), object->layouts.end(),
					[](VkImageLayout l) { return l != VK_IMAGE_LAYOUT_UNDEFINED && l != VK_IMAGE_LAYOUT_PREINITIALIZED; });
				if (!defined) continue;
				if (info.samples != VK_SAMPLE_COUNT_1_BIT) {
					warnings_.push_back("multisampled images are not captured; they start empty on replay");
					continue;
				}
				if (!(info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
					warnings_.push_back(std::string("images of format ") + (EnumName(info.format) ? EnumName(info.format) : "?") + " cannot be copied; they start empty on replay");
					continue;
				}
				const u32 aspects = AspectsOf(info.format);
				for (u32 bit = 1; bit <= VK_IMAGE_ASPECT_STENCIL_BIT; bit <<= 1) {
					if (!(aspects & bit)) continue;
					const VkImageAspectFlagBits aspect = (VkImageAspectFlagBits)bit;
					const FormatBlock block = BlockOf(info.format, aspect);
					if (!block.bytes) {
						warnings_.push_back(std::string("format ") + (EnumName(info.format) ? EnumName(info.format) : "?") + " is not captured; its images start empty on replay");
						continue;
					}
					for (u32 layer = 0; layer < info.arrayLayers; ++layer)
						for (u32 mip = 0; mip < info.mipLevels; ++mip) {
							const VkImageLayout layout = object->layouts[(size_t)layer * info.mipLevels + mip];
							if (layout == VK_IMAGE_LAYOUT_UNDEFINED || layout == VK_IMAGE_LAYOUT_PREINITIALIZED) continue;
							const VkExtent3D extent = {std::max(1u, info.extent.width >> mip), std::max(1u, info.extent.height >> mip), std::max(1u, info.extent.depth >> mip)};
							const u64 bytes = (u64)((extent.width + block.width - 1) / block.width) * ((extent.height + block.height - 1) / block.height) * extent.depth * block.bytes;
							copies.push_back({object.get(), aspect, mip, layer, extent, bytes, 0});
						}
				}
			}
		}

		size_t at = 0;
		while (at < copies.size()) {
			// A batch of copies whose total fits the batch size (one larger copy is a batch of its own).
			u64 total = 0;
			size_t end = at;
			while (end < copies.size() && (end == at || total + AlignUp(copies[end].bytes, 16) <= kCopyBatch)) {
				copies[end].offset = total;
				total += AlignUp(copies[end].bytes, 16);
				++end;
			}
			Readback readback;
			if (!MakeReadback(device_, std::max<u64>(total, 16), readback)) {
				warnings_.push_back("the capture could not get readback memory; some resources start empty on replay");
				FreeReadback(device_, readback);
				break;
			}
			VkCommandBuffer cb = BeginOwn();
			std::vector<VkImageMemoryBarrier> toCopy, back;
			std::vector<Object*> images;
			for (size_t i = at; i < end; ++i)
				if (copies[i].aspect && std::find(images.begin(), images.end(), copies[i].object) == images.end()) images.push_back(copies[i].object);
			for (Object* image : images) {
				const VkImageCreateInfo& info = image->imageInfo;
				const VkImage handle = (VkImage)(uintptr_t)image->handle;
				for (u32 layer = 0; layer < info.arrayLayers; ++layer)
					for (u32 mip = 0; mip < info.mipLevels; ++mip) {
						const VkImageLayout layout = image->layouts[(size_t)layer * info.mipLevels + mip];
						if (layout == VK_IMAGE_LAYOUT_UNDEFINED || layout == VK_IMAGE_LAYOUT_PREINITIALIZED) continue;
						toCopy.push_back(ImageBarrier(handle, AspectsOf(info.format), mip, layer, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL));
						back.push_back(ImageBarrier(handle, AspectsOf(info.format), mip, layer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout));
					}
			}
			if (!toCopy.empty())
				device_.next.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, (u32)toCopy.size(), toCopy.data());
			for (size_t i = at; i < end; ++i) {
				const Copy& copy = copies[i];
				if (!copy.aspect) {
					VkBufferCopy region = {0, copy.offset, copy.bytes};
					device_.next.vkCmdCopyBuffer(cb, (VkBuffer)(uintptr_t)copy.object->handle, readback.buffer, 1, &region);
					continue;
				}
				VkBufferImageCopy region = {};
				region.bufferOffset = copy.offset;
				region.imageSubresource = {(VkImageAspectFlags)copy.aspect, copy.mip, copy.layer, 1};
				region.imageExtent = copy.extent;
				device_.next.vkCmdCopyImageToBuffer(cb, (VkImage)(uintptr_t)copy.object->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
			}
			if (!back.empty())
				device_.next.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, (u32)back.size(), back.data());
			if (!SubmitOwn(cb, {}, VK_NULL_HANDLE)) warnings_.push_back("the capture's copies did not finish; some resources start empty on replay");
			for (size_t i = at; i < end; ++i) {
				const Copy& copy = copies[i];
				ByteWriter payload;
				Ser s(payload, &device_);
				if (!copy.aspect) {
					BufferContents contents = {copy.object->id, readback.mapped + copy.offset, copy.bytes};
					Serialize(s, contents);
					file_.Record(kBufferContents, payload);
				}
				else {
					ImageContents contents = {copy.object->id, (u32)copy.aspect, copy.mip, copy.layer, copy.extent, readback.mapped + copy.offset, copy.bytes};
					Serialize(s, contents);
					file_.Record(kImageContents, payload);
				}
			}
			FreeReadback(device_, readback);
			at = end;
		}
	}

	// --- During the frame ------------------------------------------------------------------------------------------

	// CPU writes to mapped memory since the last look: each changed run of blocks as a MapWrite.
	void FrameCapture::DiffMappings() {
		for (auto& object : device_.objects) {
			if (!object->alive || object->kind != ObjectKind::Memory || !object->mapped) continue;
			Shadow* shadow = nullptr;
			for (Shadow& candidate : shadows_)
				if (candidate.memory == object->id) shadow = &candidate;
			const u8* now = (const u8*)object->mapped;
			if (!shadow) {
				// Mapped (or made) during the frame: everything in it counts as written.
				MapWrite write = {object->id, object->mappedOffset, now, object->mappedSize};
				Record(kMapWrite, write);
				shadows_.push_back({object->id, object->mappedOffset, std::vector<u8>(now, now + object->mappedSize)});
				continue;
			}
			// The part of the mapping the shadow covers.
			const u64 from = std::max(object->mappedOffset, shadow->offset);
			const u64 to = std::min(object->mappedOffset + object->mappedSize, shadow->offset + shadow->bytes.size());
			u64 run = 0;
			bool inRun = false;
			for (u64 at = from; at < to; at += kDiffBlock) {
				const u64 length = std::min<u64>(kDiffBlock, to - at);
				const u8* current = now + (at - object->mappedOffset);
				u8* old = shadow->bytes.data() + (at - shadow->offset);
				const bool changed = memcmp(current, old, length) != 0;
				if (changed && !inRun) {
					run = at;
					inRun = true;
				}
				if ((!changed || at + length >= to) && inRun) {
					const u64 runEnd = changed ? at + length : at;
					MapWrite write = {object->id, run, now + (run - object->mappedOffset), runEnd - run};
					Record(kMapWrite, write);
					memcpy(shadow->bytes.data() + (run - shadow->offset), write.data, write.size);
					inRun = false;
				}
			}
		}
	}

	void FrameCapture::BeforeSubmit(VkQueue queue, const std::vector<std::vector<VkCommandBuffer>>& batches, const std::vector<std::vector<VkSemaphore>>& waits,
		const std::vector<std::vector<VkSemaphore>>& signals, VkFence fence) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		DiffMappings();
		std::vector<SubmitBatch> out(batches.size());
		std::vector<std::vector<CommandBufferContents>> contents(batches.size());
		std::vector<std::vector<u64>> waitIds(batches.size()), signalIds(batches.size());
		for (size_t b = 0; b < batches.size(); ++b) {
			for (VkCommandBuffer cb : batches[b]) {
				Object* object = device_.Find(cb);
				if (!object || !object->recording) {
					Warn("a command buffer the layer did not see made was submitted; its commands are missing");
					continue;
				}
				const Recording& recording = *object->recording;
				contents[b].push_back({object->id, recording.commands.Data(), recording.commands.Size(), recording.count});
				events_ += recording.count;
				Unsupported(recording);
			}
			for (VkSemaphore semaphore : waits[b]) waitIds[b].push_back(device_.Id((const void*)semaphore));
			for (VkSemaphore semaphore : signals[b]) signalIds[b].push_back(device_.Id((const void*)semaphore));
			out[b] = {(u32)waitIds[b].size(), waitIds[b].data(), (u32)contents[b].size(), contents[b].data(), (u32)signalIds[b].size(), signalIds[b].data()};
		}
		Submit submit = {device_.Id((const void*)queue), (u32)out.size(), out.data(), device_.Id((const void*)fence)};
		Record(kSubmit, submit);
	}

	void FrameCapture::QueueLabel(VkQueue queue, LabelOp op, const VkDebugUtilsLabelEXT* label) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		fb::vk::QueueLabel record = {device_.Id((const void*)queue), op, label && label->pLabelName ? label->pLabelName : "", {}};
		if (label) memcpy(record.color, label->color, sizeof(record.color));
		Record(kQueueLabel, record);
		events_ += 1;
	}

	// Objects made (or named) while a frame is captured: their records go in the frame, in order.
	void FrameCapture::Created(Object* object) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		RawRecord(stream_, object->record, object->create);
		if (!object->name.empty()) {
			ObjectName name = {object->id, object->name};
			Record(kName, name);
		}
	}

	void FrameCapture::Named(Object* object) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		ObjectName name = {object->id, object->name};
		Record(kName, name);
	}

	void FrameCapture::Destroyed(Object* object) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		ObjectRef ref = {object->id};
		Record(kDestroyed, ref);
	}

	void FrameCapture::DescriptorsUpdated(const ByteWriter& record) {
		std::lock_guard<std::recursive_mutex> guard(device_.lock);
		if (!active_) return;
		RawRecord(stream_, kDescriptorUpdate, record);
	}

	// --- The frame's end ---------------------------------------------------------------------------------------------

	// The presented image, copied before it is presented (the copy waits for the program's semaphores; the present for
	// the copy), as a PNG beside the capture and a thumbnail record.
	void FrameCapture::Thumbnail(VkQueue queue, const VkPresentInfoKHR& info, std::vector<VkSemaphore>& waits, CaptureSummary& summary) {
		if (!info.swapchainCount || !MakeObjects(queue)) return;
		Object* chain = device_.Find(info.pSwapchains[0]);
		const u32 index = info.pImageIndices[0];
		if (!chain || index >= chain->images.size()) return;
		Object* image = device_.FindId(chain->images[index]);
		if (!image || !(image->imageInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return;
		const VkImageCreateInfo& imageInfo = image->imageInfo;
		summary.width = imageInfo.extent.width;
		summary.height = imageInfo.extent.height;
		const FormatInfo* format = FormatOf(imageInfo.format, VK_IMAGE_ASPECT_COLOR_BIT);
		if (!format || IsBlock(format->codec)) return;
		const u32 width = imageInfo.extent.width, height = imageInfo.extent.height;
		const u64 rowPitch = (u64)width * format->bytes;
		Readback readback;
		if (!MakeReadback(device_, rowPitch * height, readback)) {
			FreeReadback(device_, readback);
			return;
		}
		const VkImage handle = (VkImage)(uintptr_t)image->handle;
		const VkImageLayout layout = image->layouts.empty() ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : image->layouts[0];
		VkCommandBuffer cb = BeginOwn();
		VkImageMemoryBarrier toCopy = ImageBarrier(handle, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		device_.next.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toCopy);
		VkBufferImageCopy region = {};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {width, height, 1};
		device_.next.vkCmdCopyImageToBuffer(cb, handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
		VkImageMemoryBarrier back = ImageBarrier(handle, VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout);
		device_.next.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &back);
		if (!SubmitOwn(cb, waits, copied_)) {
			FreeReadback(device_, readback);
			return;
		}
		waits.assign(1, copied_);
		const u32 step = std::max<u32>(1, (width + 479) / 480);
		const u32 outWidth = width / step, outHeight = height / step;
		std::vector<float> row((size_t)width * 4);
		std::vector<u8> pixels((size_t)outWidth * outHeight * 4);
		const bool linear = format->component == Component::Float || format->component == Component::Srgb;
		for (u32 y = 0; y < outHeight; ++y) {
			DecodeRows(*format, readback.mapped + rowPitch * (size_t)(y * step), rowPitch, width, 1, row.data());
			for (u32 x = 0; x < outWidth; ++x)
				for (int c = 0; c < 4; ++c) {
					float v = row[(size_t)(x * step) * 4 + c];
					if (std::isnan(v)) v = 0;
					if (c < 3 && linear) v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
					if (c == 3) v = 1.0f;
					pixels[((size_t)y * outWidth + x) * 4 + c] = (u8)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
				}
		}
		FreeReadback(device_, readback);
		summary.thumbnail = WritePng(base_ + L".png", outWidth, outHeight, pixels.data(), (size_t)outWidth * 4);
		ByteWriter thumbnail;
		thumbnail.Put(outWidth);
		thumbnail.Put(outHeight);
		thumbnail.Bytes(pixels.data(), pixels.size());
		file_.Record(kRecordThumbnail, thumbnail);
	}

	static std::string DriverText(const std::string& info, u32 version, u32 vendor) {
		if (!info.empty()) return info;
		// NVIDIA packs 10.8.8.6 bits; others follow the API's version packing.
		if (vendor == 0x10DE) return std::to_string(version >> 22) + "." + std::to_string((version >> 14) & 0xFF);
		return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." + std::to_string(VK_API_VERSION_PATCH(version));
	}

	std::string FrameCapture::MetaJson(const CaptureSummary& summary) {
		JsonWriter json;
		json.BeginObject();
		json.String("program", ExecutableName());
		json.String("programPath", Narrow(ExecutablePath()));
		json.Uint("pid", GetCurrentProcessId());
		json.String("api", "Vulkan");
		json.Uint("frame", summary.frame);
		json.String("time", TimeStamp());
		json.String("gpu", summary.gpu);
		json.String("driver", summary.driver);
		json.Uint("width", summary.width);
		json.Uint("height", summary.height);
		json.Uint("events", summary.events);
		json.String("comment", Session::Get().Comment());
		json.BeginArray("warnings");
		for (const std::string& warning : summary.warnings) json.String(warning);
		json.EndArray();
		json.BeginObject("annotations");
		for (const auto& annotation : Session::Get().Annotations()) json.String(annotation.first.c_str(), annotation.second);
		json.EndObject();
		json.EndObject();
		return json.Take();
	}

	void FrameCapture::End(VkQueue queue, u64 swapchain, u32 imageIndex, CaptureSummary& summary) {
		Object* chain = device_.FindId(swapchain);
		const u64 image = chain && imageIndex < chain->images.size() ? chain->images[imageIndex] : 0;
		Present present = {device_.Id((const void*)queue), swapchain, imageIndex, image};
		Record(kPresent, present);
		events_ += 1;
		active_ = false;
		Session::Get().SetCapturing(false);
		device_.next.vkDeviceWaitIdle(device_.handle);
		VkPhysicalDeviceProperties properties = {};
		device_.instance->next.vkGetPhysicalDeviceProperties(device_.physical, &properties);
		VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
		if (device_.instance->next.vkGetPhysicalDeviceProperties2 && properties.apiVersion >= VK_API_VERSION_1_2) {
			VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
			device_.instance->next.vkGetPhysicalDeviceProperties2(device_.physical, &properties2);
		}
		summary.api = "Vulkan";
		summary.frame = frame_;
		summary.events = events_;
		summary.gpu = std::string(properties.deviceName, strnlen(properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE));
		summary.driver = DriverText(std::string(driver.driverInfo, strnlen(driver.driverInfo, VK_MAX_DRIVER_INFO_SIZE)), properties.driverVersion, properties.vendorID);
		summary.warnings = warnings_;
		file_.Append(stream_.Data(), stream_.Size());
		file_.Record(kRecordFrameEnd, nullptr, 0);
		for (const auto& annotation : Session::Get().Annotations()) {
			ByteWriter payload;
			std::string key = annotation.first, value = annotation.second;
			Ser s(payload, &device_);
			s.Text("key", key);
			s.Text("value", value);
			file_.Record(kRecordAnnotation, payload);
		}
		for (const std::string& warning : warnings_) file_.Record(kRecordWarning, warning.data(), warning.size());
		const std::string meta = MetaJson(summary);
		file_.Record(kRecordMeta, meta.data(), meta.size());
		const u64 bytes = file_.Written();
		std::string error;
		if (!file_.Close(error)) {
			Session::Get().Failed(error);
			return;
		}
		stream_.Clear();
		shadows_.clear();
		Session::Get().Written(base_, bytes, summary);
	}
}
