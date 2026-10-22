// The Vulkan replay's requests (replay.h): opening a capture, its events and calls, its resources and the replay's
// messages; reads after an event are cached so pixel picks at the same place need no new replay.
#include "vkreplay.h"

namespace fb::replay
{
	std::unique_ptr<ApiReplay> MakeVulkanReplay() { return std::make_unique<vulkan::VulkanReplay>(); }
}

namespace fb::replay::vulkan
{
	namespace
	{
		// Notes every object a command names, for the Resources view's first and last use.
		class UsageMap : public ModelMap
		{
		public:
			UsageMap(const Model& model, std::map<u64, Usage>& usage) : ModelMap(model), usage_(usage) {}
			void* Object(u64 id) override {
				if (id) {
					Usage& usage = usage_[id];
					if (!usage.first) usage.first = eid;
					usage.last = eid;
					++usage.count;
				}
				return ModelMap::Object(id);
			}
			u32 eid = 0;

		private:
			std::map<u64, Usage>& usage_;
		};
	}

	ReplayDevice& VulkanReplay::Device() {
		if (!device_) Fail("there is no replay device: %s", deviceProblem_.empty() ? "it was not asked for" : deviceProblem_.c_str());
		return *device_;
	}

	void VulkanReplay::ComputeUsage() {
		UsageMap map(model_, usage_);
		Arena arena;
		std::vector<u8> args;
		for (const FrameOp& op : model_.frame)
			for (const BufferRef& buffer : op.buffers)
				for (const CommandRef& command : buffer.commands) {
					map.eid = command.eid;
					args.assign(CommandArgsSize(command.command) + 16, 0);
					arena.Reset();
					model_.DecodeCommandIn(arena, command, args.data(), &map);
				}
		// Commands reach resources through other objects: a framebuffer its views, a descriptor set what it holds, a
		// view its image.
		auto spread = [&](u64 from, u64 to) {
			auto source = usage_.find(from);
			if (source == usage_.end() || !to) return;
			Usage& target = usage_[to];
			if (!target.first || source->second.first < target.first) target.first = source->second.first;
			target.last = std::max(target.last, source->second.last);
			target.count += source->second.count;
		};
		for (const auto& object : model_.objects) {
			Create<VkFramebufferCreateInfo> framebuffer = {};
			if (object.second.kind != ObjectKind::Framebuffer || !model_.DecodeModel(object.second.record, object.second.recordSize, framebuffer) || !framebuffer.info.pAttachments) continue;
			for (u32 i = 0; i < framebuffer.info.attachmentCount; ++i) spread(object.first, IdOf(framebuffer.info.pAttachments[i]));
		}
		for (const auto& set : DescriptorsAt(model_, UINT32_MAX))
			for (const auto& element : set.second) {
				spread(set.first, IdOf(element.second.image.imageView));
				spread(set.first, IdOf(element.second.buffer.buffer));
				spread(set.first, IdOf(element.second.texel));
			}
		for (const auto& object : model_.objects) {
			Create<VkImageViewCreateInfo> view = {};
			if (object.second.kind == ObjectKind::ImageView && model_.DecodeModel(object.second.record, object.second.recordSize, view)) spread(object.first, IdOf(view.info.image));
		}
	}

	void VulkanReplay::Open(CaptureReader& file, const JsonValue& options, JsonWriter& info) {
		std::string error;
		if (!model_.Load(file, error)) Fail("%s", error.c_str());
		ComputeUsage();
		if (!options["noDevice"].Bool(false)) {
			try {
				device_ = std::make_unique<ReplayDevice>(model_);
				device_->Create(options);
			}
			catch (const Failure& failure) {
				deviceProblem_ = failure.message;
				device_.reset();
			}
		}
		u32 actions = 0, submissions = 0, resources = 0;
		for (const Event& event : model_.events)
			if (!event.hidden && event.kind != EventKind::Marker) ++actions;
		for (const FrameOp& op : model_.frame)
			if (op.kind == OpKind::Submit) ++submissions;
		for (const auto& object : model_.objects)
			if (object.second.kind == ObjectKind::Buffer || object.second.kind == ObjectKind::Image) ++resources;
		const DeviceInfo& d = model_.device;
		info.String("api", "Vulkan");
		info.Raw("meta", model_.meta);
		info.BeginObject("captureDevice");
		info.String("adapter", d.deviceName);
		info.Uint("vendorId", d.vendorId);
		info.Uint("deviceId", d.deviceId);
		info.String("driver", d.driverInfo.empty() ? d.driverName : d.driverInfo);
		info.String("level", Format("Vulkan %u.%u", VK_API_VERSION_MAJOR(d.apiVersion), VK_API_VERSION_MINOR(d.apiVersion)));
		info.EndObject();
		info.BeginObject("replayDevice");
		if (device_) {
			info.String("adapter", device_->deviceName);
			info.String("driver", device_->driver);
		}
		else info.String("problem", deviceProblem_.empty() ? "no device asked for" : deviceProblem_);
		info.EndObject();
		info.BeginObject("counts");
		info.Uint("events", model_.events.size());
		info.Uint("actions", actions);
		info.Uint("submissions", submissions);
		info.Uint("objects", model_.objects.size());
		info.Uint("resources", resources);
		info.EndObject();
		info.BeginArray("warnings");
		for (const std::string& warning : model_.warnings) info.String(warning);
		if (device_)
			for (const std::string& warning : device_->warnings) info.String(warning);
		info.EndArray();
	}

	void VulkanReplay::Events(JsonWriter& out) {
		// Columns: eid, the parent marker's eid (0 at the top), kind, hidden, the marker's last eid, queue, name.
		out.BeginArray("columns");
		for (const char* column : {"eid", "parent", "kind", "hidden", "last", "queue", "name"}) out.String(column);
		out.EndArray();
		out.BeginArray("events");
		for (const Event& event : model_.events) {
			out.BeginArray();
			out.Uint(event.eid);
			out.Uint(event.parent < 0 ? 0 : model_.events[(size_t)event.parent].eid);
			out.String(EventKindName(event.kind));
			out.Uint(event.hidden ? 1 : 0);
			out.Uint(event.lastEid);
			out.Uint(event.queue);
			out.String(event.name);
			out.EndArray();
		}
		out.EndArray();
		out.BeginArray("queues");
		for (const auto& object : model_.objects) {
			if (object.second.kind != ObjectKind::Queue) continue;
			QueueCreate queue = {};
			model_.DecodeModel(object.second.record, object.second.recordSize, queue);
			const bool graphics = (queue.familyFlags & VK_QUEUE_GRAPHICS_BIT) != 0, compute = (queue.familyFlags & VK_QUEUE_COMPUTE_BIT) != 0;
			out.BeginObject();
			out.Uint("id", object.first);
			out.String("name", model_.NameOf(object.first));
			out.String("type", graphics ? "DIRECT" : compute ? "COMPUTE" : "COPY");
			out.EndObject();
		}
		out.EndArray();
	}

	void VulkanReplay::ApiCalls(u32 eid, JsonWriter& out) {
		const Event* event = model_.EventAt(eid);
		if (!event) Fail("no event %u", eid);
		ModelMap map(model_);
		out.BeginArray("calls");
		const CommandRef* command = model_.CommandOf(*event);
		if (command) {
			// The state set since the previous action of the buffer, then the event's own call.
			const BufferRef& buffer = model_.frame[event->op].buffers[(size_t)event->buffer];
			size_t first = (size_t)event->command;
			while (first > 0) {
				const CommandRef& previous = buffer.commands[first - 1];
				if (IsAction(previous.command) || ClassOf(previous.command) == CmdClass::Marker) break;
				--first;
			}
			if (!IsAction(command->command) && event->kind != EventKind::Marker) first = (size_t)event->command;
			std::vector<u8> args;
			Arena arena;
			for (size_t i = first; i <= (size_t)event->command; ++i) {
				const CommandRef& call = buffer.commands[i];
				out.BeginObject();
				out.Uint("eid", call.eid);
				out.String("name", Model::CommandName(call));
				out.Key("args");
				out.BeginObject();
				args.assign(CommandArgsSize(call.command) + 16, 0);
				model_.DecodeCommand(call, args.data(), &map, &out);
				out.EndObject();
				out.EndObject();
			}
		}
		else {
			const FrameOp& op = model_.frame[event->op];
			out.BeginObject();
			out.Uint("eid", eid);
			out.String("name", event->name);
			out.Key("args");
			out.BeginObject();
			if (op.kind == OpKind::Present) {
				Present present = {};
				model_.Decode(op.data, op.size, present, &map, &out);
			}
			else if (op.kind == OpKind::QueueLabel) {
				QueueLabel label = {};
				model_.Decode(op.data, op.size, label, &map, &out);
			}
			out.EndObject();
			out.EndObject();
		}
		out.EndArray();
	}

	static std::string MemoryKind(const Model& model, u64 memoryId) {
		MemoryCreate memory = {};
		if (!memoryId || !model.DecodeObject(memoryId, memory)) return "unbound";
		if ((memory.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (memory.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) return "device local, host visible";
		if (memory.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) return "device local";
		if (memory.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) return "host cached";
		return "host visible";
	}

	void VulkanReplay::Resources(JsonWriter& out) {
		out.BeginArray("resources");
		for (const auto& object : model_.objects) {
			const ObjectEntry& entry = object.second;
			if (entry.kind != ObjectKind::Buffer && entry.kind != ObjectKind::Image) continue;
			out.BeginObject();
			out.Uint("id", object.first);
			out.String("name", model_.NameOf(object.first));
			u64 initial = 0;
			if (entry.kind == ObjectKind::Buffer) {
				BufferCreate record = {};
				if (!model_.DecodeModel(entry.record, entry.recordSize, record)) {
					out.EndObject();
					continue;
				}
				out.String("dimension", "BUFFER");
				out.Uint("width", record.info.size);
				out.Uint("height", 1);
				out.Uint("depthOrArraySize", 1);
				out.Uint("mipLevels", 1);
				out.String("format", "UNKNOWN");
				out.Uint("samples", 1);
				out.String("flags", Flags_VkBufferUsageFlagBits(record.info.usage));
				out.String("heap", MemoryKind(model_, record.memory));
				out.String("kind", "buffer");
				auto contents = model_.bufferContents.find(object.first);
				if (contents != model_.bufferContents.end()) initial = contents->second.second;
			}
			else {
				ImageCreate record = {};
				if (!model_.DecodeModel(entry.record, entry.recordSize, record)) {
					out.EndObject();
					continue;
				}
				const VkImageCreateInfo& i = record.info;
				out.String("dimension", i.imageType == VK_IMAGE_TYPE_3D ? "TEXTURE3D" : i.imageType == VK_IMAGE_TYPE_1D ? "TEXTURE1D" : "TEXTURE2D");
				out.Uint("width", i.extent.width);
				out.Uint("height", i.extent.height);
				out.Uint("depthOrArraySize", i.imageType == VK_IMAGE_TYPE_3D ? i.extent.depth : i.arrayLayers);
				out.Uint("mipLevels", i.mipLevels);
				out.String("format", EnumName(i.format) ? EnumName(i.format) : "?");
				out.Uint("samples", (u32)i.samples);
				out.String("flags", Flags_VkImageUsageFlagBits(i.usage));
				out.String("heap", record.swapchain ? "swap chain" : MemoryKind(model_, record.memory));
				out.String("kind", record.swapchain ? "swapchain" : "image");
				auto contents = model_.imageContents.find(object.first);
				if (contents != model_.imageContents.end())
					for (const ImageData& data : contents->second) initial += data.size;
				auto layouts = model_.layouts.find(object.first);
				if (layouts != model_.layouts.end() && !layouts->second.empty() && EnumName(layouts->second[0])) out.String("state", EnumName(layouts->second[0]));
			}
			out.Bool("inFrame", entry.inFrame);
			out.Uint("initialBytes", initial);
			auto usage = usage_.find(object.first);
			if (usage != usage_.end()) {
				out.Uint("firstUse", usage->second.first);
				out.Uint("lastUse", usage->second.last);
				out.Uint("uses", usage->second.count);
			}
			out.EndObject();
		}
		out.EndArray();
	}

	void VulkanReplay::Messages(JsonWriter& out) {
		out.BeginArray("messages");
		if (device_)
			for (const ReplayDevice::Message& message : device_->messages) {
				out.BeginObject();
				out.Uint("eid", message.eid);
				out.String("severity", message.severity);
				out.String("text", message.text);
				out.EndObject();
			}
		out.EndArray();
	}

	// --- Reads after an event --------------------------------------------------------------------------------------

	void VulkanReplay::ReplayTo(u32 eid) {
		if (replayed_ && replayedTo_ == eid) return;
		Device().ReplayTo(eid);
		replayedTo_ = eid;
		replayed_ = true;
	}

	const Readback& VulkanReplay::Read(u32 eid, u64 id, VkImageAspectFlagBits aspect, u32 mip, u32 layer) {
		if (last_.valid && last_.eid == eid && last_.id == id && last_.aspect == (u32)aspect && last_.mip == mip && last_.layer == layer) return last_;
		ReplayTo(eid);
		last_.valid = false;
		Device().ReadImage(id, aspect, mip, layer, last_.bytes);
		last_.eid = eid;
		last_.id = id;
		last_.aspect = aspect;
		last_.mip = mip;
		last_.layer = layer;
		last_.valid = true;
		return last_;
	}

	void VulkanReplay::ReadBuffer(u32 eid, u64 id, u64 offset, u64 size, std::vector<u8>& out) {
		ReplayTo(eid);
		Device().ReadBuffer(id, offset, size, out);
	}
}
