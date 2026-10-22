#include "vkmodel.h"

#include "json.h"
#include "replay/replay.h"

namespace fb::replay::vulkan
{
	const char* EventKindName(EventKind kind) {
		static const char* names[] = {"marker", "draw", "dispatch", "copy", "clear", "resolve", "barrier", "state", "query", "present", "fence", "other"};
		return names[(u32)kind];
	}

	std::string ModelMap::Name(u64 id) { return model_.NameOf(id); }

	bool Model::DecodeCommand(const CommandRef& command, void* args, SerReadMap* map, JsonWriter* describe) const {
		ByteReader reader(command.args, command.size);
		Ser s(reader, const_cast<Arena&>(arena_), map, describe);
		SerializeCommand(s, command.command, args);
		return !s.Failed();
	}

	const ObjectEntry* Model::Find(u64 id) const {
		auto found = objects.find(id);
		return found == objects.end() ? nullptr : &found->second;
	}

	std::string Model::NameOf(u64 id) const {
		const ObjectEntry* object = Find(id);
		if (!object) return id ? Format("#%llu", (unsigned long long)id) : std::string();
		if (!object->name.empty()) return object->name;
		return Format("%s #%llu", ObjectKindName(object->kind), (unsigned long long)id);
	}

	const CommandRef* Model::CommandOf(const Event& event) const {
		if (event.buffer < 0 || event.op >= frame.size()) return nullptr;
		const FrameOp& op = frame[event.op];
		if ((size_t)event.buffer >= op.buffers.size() || (size_t)event.command >= op.buffers[event.buffer].commands.size()) return nullptr;
		return &op.buffers[event.buffer].commands[event.command];
	}

	std::string Model::CommandName(const CommandRef& command) {
		switch (command.command) {
			case Cmd::SetViewport: return command.flags ? "vkCmdSetViewportWithCount" : "vkCmdSetViewport";
			case Cmd::SetScissor: return command.flags ? "vkCmdSetScissorWithCount" : "vkCmdSetScissor";
			case Cmd::BindVertexBuffers: return command.flags ? "vkCmdBindVertexBuffers2" : "vkCmdBindVertexBuffers";
			case Cmd::BindIndexBuffer: return command.flags ? "vkCmdBindIndexBuffer2KHR" : "vkCmdBindIndexBuffer";
			case Cmd::WriteTimestamp: return command.flags ? "vkCmdWriteTimestamp2" : "vkCmdWriteTimestamp";
			case Cmd::BeginLabel: return "vkCmdBeginDebugUtilsLabelEXT";
			case Cmd::EndLabel: return "vkCmdEndDebugUtilsLabelEXT";
			case Cmd::InsertLabel: return "vkCmdInsertDebugUtilsLabelEXT";
			case Cmd::ExecuteCommands: return command.flags ? "(end of secondary)" : "vkCmdExecuteCommands";
			case Cmd::SetDynamic: {
				SetDynamicArgs args = {};
				ByteReader reader(command.args, command.size);
				args.state = (VkDynamicState)reader.Get<u32>();
				switch (args.state) {
					case VK_DYNAMIC_STATE_CULL_MODE: return "vkCmdSetCullMode";
					case VK_DYNAMIC_STATE_FRONT_FACE: return "vkCmdSetFrontFace";
					case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY: return "vkCmdSetPrimitiveTopology";
					case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE: return "vkCmdSetDepthTestEnable";
					case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE: return "vkCmdSetDepthWriteEnable";
					case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP: return "vkCmdSetDepthCompareOp";
					case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE: return "vkCmdSetDepthBoundsTestEnable";
					case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE: return "vkCmdSetStencilTestEnable";
					case VK_DYNAMIC_STATE_STENCIL_OP: return "vkCmdSetStencilOp";
					case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE: return "vkCmdSetRasterizerDiscardEnable";
					case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE: return "vkCmdSetDepthBiasEnable";
					case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE: return "vkCmdSetPrimitiveRestartEnable";
					default: return "vkCmdSetDynamicState";
				}
			}
			default: return std::string("vkCmd") + CmdName(command.command);
		}
	}

	// One argument's value as a label shows it: numbers as they are, objects by name, structures and arrays shortened.
	static std::string ValueText(const JsonValue& value) {
		switch (value.kind) {
			case JsonKind::Null: return "null";
			case JsonKind::Bool: return value.boolean ? "TRUE" : "FALSE";
			case JsonKind::Number: return value.number >= 1.8e19 ? "VK_WHOLE_SIZE" : value.Dump();
			case JsonKind::String: return value.text;
			case JsonKind::Array: return Format("[%zu]", value.items.size());
			case JsonKind::Object:
				if (value.Has("$ref")) return value["name"].Str("?");
				return "{...}";
		}
		return "";
	}

	std::string Model::Label(const CommandRef& command) const {
		ModelMap map(*this);
		if (command.command == Cmd::BeginLabel || command.command == Cmd::InsertLabel) {
			LabelArgs label = {};
			DecodeCommand(command, &label, &map);
			return label.name ? label.name : "(label)";
		}
		JsonWriter json;
		json.BeginObject();
		std::vector<u8> args(CommandArgsSize(command.command) + 64);
		DecodeCommand(command, args.data(), &map, &json);
		json.EndObject();
		JsonValue parsed;
		std::string error;
		std::string text = CommandName(command) + "(";
		if (JsonValue::Parse(json.Text().data(), json.Text().size(), parsed, error)) {
			bool first = true;
			for (size_t i = 0; i < parsed.members.size(); ++i) {
				const auto& member = parsed.members[i];
				// The count of the array that follows it says nothing the array does not.
				const bool count = member.first.size() > 5 && member.first.compare(member.first.size() - 5, 5, "Count") == 0;
				if (count && i + 1 < parsed.members.size() && (parsed.members[i + 1].second.kind == JsonKind::Array || parsed.members[i + 1].second.kind == JsonKind::Null)) continue;
				if (!first) text += ", ";
				first = false;
				text += ValueText(member.second);
			}
		}
		text += ")";
		return text;
	}

	// A submission's command buffers and each one's commands (CommandHeader and arguments, repeated).
	void Model::ParseSubmit(FrameOp& op) {
		Submit submit = {};
		if (!DecodeModel(op.data, op.size, submit)) return;
		op.queue = submit.queue;
		for (u32 b = 0; b < submit.batchCount; ++b) {
			const SubmitBatch& batch = submit.batches[b];
			for (u32 c = 0; c < batch.commandBufferCount; ++c) {
				const CommandBufferContents& contents = batch.commandBuffers[c];
				BufferRef buffer;
				buffer.commandBuffer = contents.commandBuffer;
				ByteReader reader(contents.commands, (size_t)contents.size);
				while (!reader.AtEnd() && !reader.Failed()) {
					CommandHeader header = reader.Get<CommandHeader>();
					const u8* args = reader.View(header.size);
					if (!args && header.size) break;
					if (header.command >= (u16)Cmd::Count) continue;
					buffer.commands.push_back({(Cmd)header.command, header.flags, args, header.size, 0});
				}
				op.buffers.push_back(std::move(buffer));
			}
		}
	}

	bool Model::Load(CaptureReader& file, std::string& error) {
		if (file.GetApi() != Api::Vulkan) {
			error = "not a Vulkan capture";
			return false;
		}
		bool inFrame = false;
		for (const RecordRef& record : file.Records()) {
			const u8* data = file.Payload(record);
			const u64 size = record.size;
			switch (record.type) {
				case kRecordMeta: meta.assign((const char*)data, (size_t)size); continue;
				case kRecordWarning: warnings.emplace_back((const char*)data, (size_t)size); continue;
				case kRecordFrameBegin: inFrame = true; continue;
				case kRecordFrameEnd: inFrame = false; continue;
				case kRecordAnnotation: continue;
				case kRecordThumbnail: {
					ByteReader reader(data, (size_t)size);
					thumbnailWidth = reader.Get<u32>();
					thumbnailHeight = reader.Get<u32>();
					thumbnail = reader.Position();
					continue;
				}
				case kDevice:
					if (!DecodeModel(data, size, device)) {
						error = "the capture's device record is damaged";
						return false;
					}
					continue;
				case kName: {
					ObjectName name = {};
					if (DecodeModel(data, size, name) && objects.count(name.id)) objects[name.id].name = name.name;
					continue;
				}
				case kImageLayout: {
					ImageLayout layout = {};
					if (DecodeModel(data, size, layout)) layouts[layout.image].assign(layout.layouts, layout.layouts + layout.count);
					continue;
				}
				case kMemoryContents: {
					MemoryContents contents = {};
					if (DecodeModel(data, size, contents)) memoryContents.push_back({contents.memory, contents.offset, (const u8*)contents.data, contents.size});
					continue;
				}
				case kBufferContents: {
					BufferContents contents = {};
					if (DecodeModel(data, size, contents)) bufferContents[contents.buffer] = {(const u8*)contents.data, contents.size};
					continue;
				}
				case kImageContents: {
					ImageContents contents = {};
					if (DecodeModel(data, size, contents))
						imageContents[contents.image].push_back({contents.aspect, contents.mip, contents.layer, contents.extent, (const u8*)contents.data, contents.size});
					continue;
				}
				case kDescriptorUpdate:
					if (!inFrame) setupDescriptors.push_back({data, size});
					else frame.push_back({OpKind::DescriptorUpdate, record.type, data, size});
					continue;
				case kSubmit: {
					FrameOp op = {OpKind::Submit, record.type, data, size};
					ParseSubmit(op);
					frame.push_back(std::move(op));
					continue;
				}
				case kMapWrite: frame.push_back({OpKind::MapWrite, record.type, data, size}); continue;
				case kPresent: {
					FrameOp op = {OpKind::Present, record.type, data, size};
					Present present = {};
					if (DecodeModel(data, size, present)) op.queue = present.queue;
					frame.push_back(std::move(op));
					continue;
				}
				case kQueueLabel: {
					FrameOp op = {OpKind::QueueLabel, record.type, data, size};
					QueueLabel label = {};
					if (DecodeModel(data, size, label)) op.queue = label.queue;
					frame.push_back(std::move(op));
					continue;
				}
				case kDestroyed: frame.push_back({OpKind::Destroyed, record.type, data, size}); continue;
				default: break;
			}
			const ObjectKind kind = KindOfRecord(record.type);
			if (kind == ObjectKind::Unknown || size < 8) continue;
			u64 id = 0;
			memcpy(&id, data, 8);
			auto found = objects.find(id);
			if (found != objects.end()) {
				// Recorded again (bound during the frame): the latest record holds the binding.
				found->second.record = data;
				found->second.recordSize = size;
				continue;
			}
			ObjectEntry& entry = objects[id];
			entry.id = id;
			entry.kind = kind;
			entry.recordType = record.type;
			entry.record = data;
			entry.recordSize = size;
			entry.inFrame = inFrame;
			if (inFrame) frame.push_back({OpKind::Create, record.type, data, size});
		}
		if (!device.id) {
			error = "the capture has no device record";
			return false;
		}
		// A swap chain's images are named after it.
		for (auto& object : objects) {
			ImageCreate image = {};
			if (object.second.kind != ObjectKind::Image || !object.second.name.empty() || !DecodeModel(object.second.record, object.second.recordSize, image) || !image.swapchain) continue;
			object.second.name = Format("%s image %u", NameOf(image.swapchain).c_str(), image.swapchainIndex);
		}
		BuildEvents();
		return true;
	}

	static EventKind KindOf(Cmd command) {
		switch (ClassOf(command)) {
			case CmdClass::Draw: return EventKind::Draw;
			case CmdClass::Dispatch: return EventKind::Dispatch;
			case CmdClass::Copy: return EventKind::Copy;
			case CmdClass::Clear: return EventKind::Clear;
			case CmdClass::Resolve: return EventKind::Resolve;
			case CmdClass::Barrier: return EventKind::Barrier;
			case CmdClass::Marker: return EventKind::Marker;
			case CmdClass::Query: return EventKind::Query;
			case CmdClass::Other: return EventKind::Other;
			default: return EventKind::State;
		}
	}

	void Model::BuildEvents() {
		std::map<u64, std::vector<i32>> stacks;
		for (u32 i = 0; i < frame.size(); ++i) {
			FrameOp& op = frame[i];
			std::vector<i32>& stack = stacks[op.queue];
			if (op.kind == OpKind::Submit) {
				op.eid = (u32)events.size() + 1;
				for (size_t b = 0; b < op.buffers.size(); ++b)
					for (size_t c = 0; c < op.buffers[b].commands.size(); ++c) {
						CommandRef& command = op.buffers[b].commands[c];
						Event event = {};
						event.eid = (u32)events.size() + 1;
						event.parent = stack.empty() ? -1 : stack.back();
						event.kind = KindOf(command.command);
						event.queue = op.queue;
						event.op = i;
						event.buffer = (i32)b;
						event.command = (i32)c;
						command.eid = event.eid;
						if (command.command == Cmd::EndLabel) {
							event.hidden = true;
							if (!stack.empty()) {
								events[(size_t)stack.back()].lastEid = event.eid;
								stack.pop_back();
							}
						}
						else if (event.kind == EventKind::State || event.kind == EventKind::Barrier || event.kind == EventKind::Query || command.command == Cmd::ExecuteCommands)
							event.hidden = true;
						event.name = Label(command);
						if (command.command == Cmd::InsertLabel) event.lastEid = event.eid;
						events.push_back(std::move(event));
						if (command.command == Cmd::BeginLabel) stack.push_back((i32)events.size() - 1);
					}
				continue;
			}
			if (op.kind != OpKind::QueueLabel && op.kind != OpKind::Present) continue;
			Event event = {};
			event.eid = (u32)events.size() + 1;
			event.parent = stack.empty() ? -1 : stack.back();
			event.queue = op.queue;
			event.op = i;
			op.eid = event.eid;
			if (op.kind == OpKind::Present) {
				Present present = {};
				DecodeModel(op.data, op.size, present);
				event.kind = EventKind::Present;
				event.name = present.swapchain ? "vkQueuePresentKHR" : "EndFrame";
				event.parent = -1;
				events.push_back(std::move(event));
				continue;
			}
			QueueLabel label = {};
			DecodeModel(op.data, op.size, label);
			event.kind = EventKind::Marker;
			if (label.op == LabelOp::End) {
				event.hidden = true;
				event.name = "vkQueueEndDebugUtilsLabelEXT";
				if (!stack.empty()) {
					events[(size_t)stack.back()].lastEid = event.eid;
					stack.pop_back();
				}
				events.push_back(std::move(event));
				continue;
			}
			event.name = label.name;
			if (label.op == LabelOp::Insert) event.lastEid = event.eid;
			events.push_back(std::move(event));
			if (label.op == LabelOp::Begin) stack.push_back((i32)events.size() - 1);
		}
		// A marker left open ends with the frame.
		for (auto& entry : stacks)
			for (i32 index : entry.second) events[(size_t)index].lastEid = (u32)events.size();
	}
}
