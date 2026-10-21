#include "model.h"

namespace fb::replay::d3d12
{
	std::string ModelMap::Name(u64 id) { return model_.NameOf(id); }

	const char* EventKindName(EventKind kind) {
		static const char* names[] = {"marker", "draw", "dispatch", "copy", "clear", "resolve", "barrier", "state", "query", "present", "fence", "other"};
		return names[(u32)kind];
	}

	static ObjectKind KindOfRecord(u32 type) {
		switch (type) {
			case kCreateHeap: return ObjectKind::Heap;
			case kCreateResource: return ObjectKind::Resource;
			case kCreateDescriptorHeap: return ObjectKind::DescriptorHeap;
			case kCreateRootSignature: return ObjectKind::RootSignature;
			case kCreatePipelineState: return ObjectKind::PipelineState;
			case kCreateCommandSignature: return ObjectKind::CommandSignature;
			case kCreateQueryHeap: return ObjectKind::QueryHeap;
			case kCreateFence: return ObjectKind::Fence;
			case kCreateCommandQueue: return ObjectKind::CommandQueue;
			case kCreateCommandList: return ObjectKind::CommandList;
			case kSwapChain: return ObjectKind::SwapChain;
			default: return ObjectKind::Unknown;
		}
	}

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
		if (event.list < 0 || event.op >= frame.size()) return nullptr;
		const FrameOp& op = frame[event.op];
		if ((size_t)event.list >= op.lists.size() || (size_t)event.command >= op.lists[event.list].commands.size()) return nullptr;
		return &op.lists[event.list].commands[event.command];
	}

	// One argument's value as a label shows it: numbers as they are, objects by name, structures and arrays shortened.
	static std::string ValueText(const JsonValue& value) {
		switch (value.kind) {
			case JsonKind::Null: return "null";
			case JsonKind::Bool: return value.boolean ? "TRUE" : "FALSE";
			case JsonKind::Number: return value.Dump();
			case JsonKind::String: return value.text;
			case JsonKind::Array: return Format("[%zu]", value.items.size());
			case JsonKind::Object:
				if (value.Has("$ref")) return value["name"].Str("?");
				if (value.Has("$heap")) return Format("%s[%lld]", value["name"].Str("heap").c_str(), (long long)value["index"].Int());
				return "{...}";
		}
		return "";
	}

	std::string Model::Label(const CommandRef& command) const {
		ModelMap map(*this);
		JsonWriter json;
		json.BeginObject();
		std::vector<u8> args(CommandArgsSize(command.command) + 64);
		DecodeCommand(command, args.data(), &map, &json);
		json.EndObject();
		if (command.command == Cmd::BeginEvent || command.command == Cmd::SetMarker) {
			MarkerArgs marker = {};
			DecodeCommand(command, &marker, &map);
			return MarkerText(marker.metadata, marker.data, marker.size);
		}
		JsonValue parsed;
		std::string error;
		std::string text = CmdName(command.command);
		text += "(";
		if (JsonValue::Parse(json.Text().data(), json.Text().size(), parsed, error)) {
			bool first = true;
			for (const auto& member : parsed.members) {
				if (!first) text += ", ";
				first = false;
				text += ValueText(member.second);
			}
		}
		text += ")";
		return text;
	}

	bool Model::Load(CaptureReader& file, std::string& error) {
		if (file.GetApi() != Api::D3D12) {
			error = "not a D3D12 capture";
			return false;
		}
		ModelMap map(*this);
		bool inFrame = false;
		for (const RecordRef& record : file.Records()) {
			const u8* data = file.Payload(record);
			const u64 size = record.size;
			switch (record.type) {
				case kRecordMeta: meta.assign((const char*)data, (size_t)size); continue;
				case kRecordWarning: warnings.emplace_back((const char*)data, (size_t)size); continue;
				case kRecordFrameBegin: inFrame = true; continue;
				case kRecordFrameEnd: inFrame = false; continue;
				case kRecordThumbnail:
					if (size >= 8) {
						memcpy(&thumbnailWidth, data, 4);
						memcpy(&thumbnailHeight, data + 4, 4);
						thumbnail = data + 8;
					}
					continue;
				case kDevice:
					if (!Decode(data, size, device, &map)) {
						error = "the device record is damaged";
						return false;
					}
					continue;
				default: break;
			}
			const ObjectKind kind = KindOfRecord(record.type);
			if (kind != ObjectKind::Unknown) {
				ObjectRef ref = {};
				ByteReader peek(data, (size_t)size);
				ref.id = peek.Get<u64>();
				ObjectEntry& entry = objects[ref.id];
				const std::string name = entry.name;
				entry = {ref.id, kind, record.type, data, size, name, inFrame};
				if (kind == ObjectKind::SwapChain) {
					SwapChainCreate chain = {};
					if (DecodeModel(data, size, chain))
						for (u32 i = 0; i < chain.bufferCount; ++i) swapChainOf[chain.buffers[i]] = chain.id;
				}
				if (inFrame) frame.push_back({OpKind::Create, record.type, data, size});
				continue;
			}
			switch (record.type) {
				case kName: {
					ObjectName name = {};
					if (DecodeModel(data, size, name)) objects[name.id].name = name.name;
					break;
				}
				case kDescriptor: {
					ByteReader peek(data, (size_t)size);
					const u64 heap = peek.Get<u64>();
					const u32 index = peek.Get<u32>();
					if (inFrame) frame.push_back({OpKind::Descriptor, record.type, data, size});
					else descriptors[heap][index] = {data, size};
					break;
				}
				case kCopyDescriptors: frame.push_back({OpKind::CopyDescriptors, record.type, data, size}); break;
				case kResourceState: {
					ResourceState state = {};
					if (DecodeModel(data, size, state)) states[state.id].assign(state.states, state.states + state.count);
					break;
				}
				case kInitialContents: {
					InitialContents contents = {};
					if (DecodeModel(data, size, contents))
						initial[contents.id].push_back({contents.subresource, contents.footprint, (const u8*)contents.data, contents.size});
					break;
				}
				case kSubmit: {
					Submit submit = {};
					if (!DecodeModel(data, size, submit)) {
						warnings.push_back("a damaged submission was skipped");
						break;
					}
					FrameOp op = {OpKind::Submit, record.type, data, size, submit.queue};
					for (u32 i = 0; i < submit.listCount; ++i) {
						ListRef list = {submit.lists[i].list};
						ByteReader commands(submit.lists[i].commands, (size_t)submit.lists[i].size);
						while (commands.Remaining() >= sizeof(CommandHeader)) {
							const CommandHeader header = commands.Get<CommandHeader>();
							const u8* args = commands.View(header.size);
							if (!args || header.command >= (u16)Cmd::Count) break;
							list.commands.push_back({(Cmd)header.command, args, header.size, 0});
						}
						op.lists.push_back(std::move(list));
					}
					frame.push_back(std::move(op));
					break;
				}
				case kMapWrite: frame.push_back({OpKind::MapWrite, record.type, data, size}); break;
				case kQueueSignal:
				case kQueueWait: {
					fb::d3d12::QueueFence fence = {};
					DecodeModel(data, size, fence);
					frame.push_back({record.type == kQueueSignal ? OpKind::Signal : OpKind::Wait, record.type, data, size, fence.queue});
					break;
				}
				case kQueueMarker: {
					fb::d3d12::QueueMarker marker = {};
					DecodeModel(data, size, marker);
					frame.push_back({OpKind::QueueMarker, record.type, data, size, marker.queue});
					break;
				}
				case kPresent: frame.push_back({OpKind::Present, record.type, data, size}); break;
				case kDestroyed: frame.push_back({OpKind::Destroyed, record.type, data, size}); break;
				case kFenceSignal: frame.push_back({OpKind::FenceSignal, record.type, data, size}); break;
				default: break;
			}
		}
		if (!device.featureLevel) {
			error = "the capture has no device record";
			return false;
		}
		// Swap chain buffers the program never named are named by their swap chain.
		std::map<u64, u32> bufferIndex;
		for (const auto& buffer : swapChainOf) {
			ObjectEntry& entry = objects[buffer.first];
			const u32 index = bufferIndex[buffer.second]++;
			if (entry.name.empty()) entry.name = Format("%s buffer %u", NameOf(buffer.second).c_str(), index);
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

	void Model::AddCommandEvents(u32 opIndex, std::map<u64, std::vector<i32>>& stacks) {
		FrameOp& op = frame[opIndex];
		std::vector<i32>& stack = stacks[op.queue];
		op.eid = (u32)events.size() + 1;
		for (size_t l = 0; l < op.lists.size(); ++l) {
			for (size_t c = 0; c < op.lists[l].commands.size(); ++c) {
				CommandRef& command = op.lists[l].commands[c];
				Event event = {};
				event.eid = (u32)events.size() + 1;
				event.parent = stack.empty() ? -1 : stack.back();
				event.kind = KindOf(command.command);
				event.queue = op.queue;
				event.op = opIndex;
				event.list = (i32)l;
				event.command = (i32)c;
				command.eid = event.eid;
				if (command.command == Cmd::EndEvent) {
					event.hidden = true;
					if (!stack.empty()) {
						events[(size_t)stack.back()].lastEid = event.eid;
						stack.pop_back();
					}
				}
				else if (event.kind == EventKind::State || event.kind == EventKind::Barrier || event.kind == EventKind::Query || command.command == Cmd::Reset)
					event.hidden = true;
				event.name = Label(command);
				if (command.command == Cmd::SetMarker) event.lastEid = event.eid;
				events.push_back(std::move(event));
				if (command.command == Cmd::BeginEvent) stack.push_back((i32)events.size() - 1);
			}
		}
	}

	void Model::BuildEvents() {
		std::map<u64, std::vector<i32>> stacks;
		ModelMap map(*this);
		for (u32 i = 0; i < frame.size(); ++i) {
			FrameOp& op = frame[i];
			if (op.kind == OpKind::Submit) {
				AddCommandEvents(i, stacks);
				continue;
			}
			if (op.kind != OpKind::QueueMarker && op.kind != OpKind::Present && op.kind != OpKind::Signal && op.kind != OpKind::Wait) continue;
			std::vector<i32>& stack = stacks[op.queue];
			Event event = {};
			event.eid = (u32)events.size() + 1;
			event.parent = stack.empty() ? -1 : stack.back();
			event.queue = op.queue;
			event.op = i;
			op.eid = event.eid;
			if (op.kind == OpKind::Present) {
				fb::d3d12::Present present = {};
				Decode(op.data, op.size, present, &map);
				event.kind = EventKind::Present;
				event.name = present.swapChain ? "Present" : "EndFrame";   // a frame bounded by StartFrame/EndFrame
				event.parent = -1;
			}
			else if (op.kind == OpKind::QueueMarker) {
				fb::d3d12::QueueMarker marker = {};
				Decode(op.data, op.size, marker, &map);
				event.kind = EventKind::Marker;
				if (marker.op == MarkerOp::End) {
					event.hidden = true;
					event.name = "EndEvent";
					if (!stack.empty()) {
						events[(size_t)stack.back()].lastEid = event.eid;
						stack.pop_back();
					}
				}
				else event.name = MarkerText(marker.metadata, marker.data, marker.size);
				if (marker.op == MarkerOp::Set) event.lastEid = event.eid;
				events.push_back(std::move(event));
				if (marker.op == MarkerOp::Begin) stack.push_back((i32)events.size() - 1);
				continue;
			}
			else {
				fb::d3d12::QueueFence fence = {};
				Decode(op.data, op.size, fence, &map);
				event.kind = EventKind::Fence;
				event.name = Format("%s(%s, %llu)", op.kind == OpKind::Signal ? "Signal" : "Wait", NameOf(fence.fence).c_str(), (unsigned long long)fence.value);
			}
			events.push_back(std::move(event));
		}
		// Markers left open at the frame's end span to its last event.
		for (Event& event : events)
			if (event.kind == EventKind::Marker && !event.hidden && event.lastEid == 0) event.lastEid = (u32)events.size();
	}
}
