#include "d3d12replay.h"
#include "d3d12_names.h"

namespace fb::replay
{
	std::unique_ptr<ApiReplay> MakeD3D12Replay() { return std::make_unique<d3d12::D3D12Replay>(); }
}

namespace fb::replay::d3d12
{
	namespace
	{
		// Notes every object and buffer a command names, for the Resources view's first and last use.
		class UsageMap : public ModelMap
		{
		public:
			UsageMap(const Model& model, std::map<u64, Usage>& usage) : ModelMap(model), usage_(usage) {}
			void* Object(u64 id) override {
				Note(id);
				return ModelMap::Object(id);
			}
			u64 Address(u64 id, u64 offset) override {
				Note(id);
				return ModelMap::Address(id, offset);
			}
			u32 eid = 0;

		private:
			void Note(u64 id) {
				if (!id) return;
				Usage& usage = usage_[id];
				if (!usage.first) usage.first = eid;
				usage.last = eid;
				++usage.count;
			}
			std::map<u64, Usage>& usage_;
		};
	}

	ReplayDevice& D3D12Replay::Device() {
		if (!device_) Fail("there is no replay device: %s", deviceProblem_.empty() ? "it was not asked for" : deviceProblem_.c_str());
		return *device_;
	}

	void D3D12Replay::ComputeUsage() {
		UsageMap map(model_, usage_);
		std::vector<u8> args;
		for (const FrameOp& op : model_.frame) {
			for (const ListRef& list : op.lists) {
				for (const CommandRef& command : list.commands) {
					if (command.command == Cmd::Unsupported) continue;
					map.eid = command.eid;
					args.assign(CommandArgsSize(command.command) + 16, 0);
					model_.DecodeCommand(command, args.data(), &map);
				}
			}
		}
	}

	void D3D12Replay::Open(CaptureReader& file, const JsonValue& options, JsonWriter& info) {
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
			if (object.second.kind == ObjectKind::Resource) ++resources;
		info.String("api", "D3D12");
		info.Raw("meta", model_.meta);
		info.BeginObject("captureDevice");
		info.String("adapter", model_.device.adapter);
		info.Uint("vendorId", model_.device.vendorId);
		info.Uint("deviceId", model_.device.deviceId);
		info.String("driver", model_.device.driver);
		info.String("level", EnumName(model_.device.featureLevel) ? std::string("FL ") + EnumName(model_.device.featureLevel) : "");
		info.EndObject();
		info.BeginObject("replayDevice");
		if (device_) {
			info.String("adapter", device_->adapterName);
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

	void D3D12Replay::Events(JsonWriter& out) {
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
			if (object.second.kind != ObjectKind::CommandQueue) continue;
			QueueCreate queue = {};
			model_.DecodeModel(object.second.record, object.second.recordSize, queue);
			out.BeginObject();
			out.Uint("id", object.first);
			out.String("name", model_.NameOf(object.first));
			out.String("type", EnumName(queue.desc.Type) ? EnumName(queue.desc.Type) : "DIRECT");
			out.EndObject();
		}
		out.EndArray();
	}

	void D3D12Replay::ApiCalls(u32 eid, JsonWriter& out) {
		const Event* event = model_.EventAt(eid);
		if (!event) Fail("no event %u", eid);
		ModelMap map(model_);
		out.BeginArray("calls");
		const CommandRef* command = model_.CommandOf(*event);
		if (command) {
			// The state set since the previous action of the list, then the event's own call.
			const ListRef& list = model_.frame[event->op].lists[(size_t)event->list];
			size_t first = (size_t)event->command;
			while (first > 0) {
				const CommandRef& previous = list.commands[first - 1];
				if (IsAction(previous.command) || previous.command == Cmd::BeginEvent || previous.command == Cmd::EndEvent || previous.command == Cmd::SetMarker) break;
				--first;
			}
			if (IsAction(command->command) || event->kind == EventKind::Marker) first = std::min(first, (size_t)event->command);
			else first = (size_t)event->command;
			std::vector<u8> args;
			for (size_t i = first; i <= (size_t)event->command; ++i) {
				const CommandRef& call = list.commands[i];
				out.BeginObject();
				out.Uint("eid", call.eid);
				out.String("name", CmdName(call.command));
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
			switch (op.kind) {
				case OpKind::Present: {
					fb::d3d12::Present present = {};
					model_.Decode(op.data, op.size, present, &map, &out);
					break;
				}
				case OpKind::Signal:
				case OpKind::Wait: {
					fb::d3d12::QueueFence fence = {};
					model_.Decode(op.data, op.size, fence, &map, &out);
					break;
				}
				case OpKind::QueueMarker: {
					fb::d3d12::QueueMarker marker = {};
					model_.Decode(op.data, op.size, marker, &map, &out);
					break;
				}
				default: break;
			}
			out.EndObject();
			out.EndObject();
		}
		out.EndArray();
	}

	void D3D12Replay::Resources(JsonWriter& out) {
		out.BeginArray("resources");
		for (const auto& object : model_.objects) {
			if (object.second.kind != ObjectKind::Resource) continue;
			ResourceCreate record = {};
			if (!model_.DecodeModel(object.second.record, object.second.recordSize, record)) continue;
			const D3D12_RESOURCE_DESC1& d = record.desc;
			out.BeginObject();
			out.Uint("id", object.first);
			out.String("name", model_.NameOf(object.first));
			out.String("dimension", EnumName(d.Dimension) ? EnumName(d.Dimension) : "?");
			out.Uint("width", d.Width);
			out.Uint("height", d.Height);
			out.Uint("depthOrArraySize", d.DepthOrArraySize);
			out.Uint("mipLevels", d.MipLevels);
			out.String("format", EnumName(d.Format) ? EnumName(d.Format) : "UNKNOWN");
			out.Uint("samples", d.SampleDesc.Count);
			out.String("flags", EnumName(d.Flags));
			out.String("heap", record.kind == ResourceKind::SwapChainBuffer ? "swap chain" : (EnumName(record.heapProperties.Type) ? EnumName(record.heapProperties.Type) : "?"));
			out.String("kind", record.kind == ResourceKind::Placed ? "placed" : record.kind == ResourceKind::Reserved ? "reserved" : record.kind == ResourceKind::SwapChainBuffer ? "swapchain" : "committed");
			out.Bool("inFrame", object.second.inFrame);
			auto initial = model_.initial.find(object.first);
			u64 bytes = 0;
			if (initial != model_.initial.end())
				for (const InitialData& data : initial->second) bytes += data.size;
			out.Uint("initialBytes", bytes);
			auto usage = usage_.find(object.first);
			if (usage != usage_.end()) {
				out.Uint("firstUse", usage->second.first);
				out.Uint("lastUse", usage->second.last);
				out.Uint("uses", usage->second.count);
			}
			auto states = model_.states.find(object.first);
			if (states != model_.states.end() && !states->second.empty()) out.String("state", EnumName(states->second[0]));
			out.EndObject();
		}
		out.EndArray();
	}

	void D3D12Replay::Messages(JsonWriter& out) {
		out.BeginArray("messages");
		if (device_) {
			for (const ReplayDevice::Message& message : device_->messages) {
				out.BeginObject();
				out.Uint("eid", message.eid);
				out.String("severity", message.severity);
				out.String("text", message.text);
				out.EndObject();
			}
		}
		out.EndArray();
	}
}
