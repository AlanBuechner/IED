// GPU time per action (docs/22 "Performance"): the frame replayed once with pipeline statistics around every draw and
// dispatch and an occlusion query around every draw (the samples that passed the depth and stencil tests), then
// `repeat` times with only a timestamp before and after every action on the direct and compute queues - the queries
// make the GPU finish each action before the next, so they would time another frame. Each action's time is the median
// of the repeats, a marker's the span of its actions in the median frame.
#include "d3d12replay.h"

#include <algorithm>
#include <set>

namespace fb::replay::d3d12
{
	namespace
	{
		class TimingHooks final : public ReplayHooks
		{
		public:
			TimingHooks(ReplayDevice& device, const Model& model) : device_(device) {
				for (const FrameOp& op : model.frame)
					for (const ListRef& list : op.lists)
						for (const CommandRef& command : list.commands)
							if (IsAction(command.command)) slots_[command.eid] = (u32)slots_.size();
				ID3D12Device* d = device.Device();
				const u32 count = std::max<u32>(1, (u32)slots_.size());
				D3D12_QUERY_HEAP_DESC times = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, count * 2};
				D3D12_QUERY_HEAP_DESC stats = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, count};
				D3D12_QUERY_HEAP_DESC samples = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, count};
				if (FAILED(d->CreateQueryHeap(&times, __uuidof(ID3D12QueryHeap), (void**)&times_)) || FAILED(d->CreateQueryHeap(&stats, __uuidof(ID3D12QueryHeap), (void**)&stats_)) ||
					FAILED(d->CreateQueryHeap(&samples, __uuidof(ID3D12QueryHeap), (void**)&samples_)))
					Fail("cannot make the timing queries");
				timeResults_ = Buffer(d, (u64)count * 2 * 8);
				statResults_ = Buffer(d, (u64)count * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
				sampleResults_ = Buffer(d, (u64)count * 8);
			}
			~TimingHooks() {
				if (times_) times_->Release();
				if (stats_) stats_->Release();
				if (samples_) samples_->Release();
				if (timeResults_) timeResults_->Release();
				if (statResults_) statResults_->Release();
				if (sampleResults_) sampleResults_->Release();
			}

			void Before(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				auto slot = slots_.find(command.eid);
				if (slot == slots_.end() || !Timed(list)) return;
				list->EndQuery(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot->second * 2);
				if (counting_ && Counted(command)) list->BeginQuery(stats_, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot->second);
				if (counting_ && ClassOf(command.command) == CmdClass::Draw) list->BeginQuery(samples_, D3D12_QUERY_TYPE_OCCLUSION, slot->second);
				used_.push_back(slot->second);
			}

			void After(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				auto slot = slots_.find(command.eid);
				if (slot == slots_.end() || !Timed(list)) return;
				if (counting_ && ClassOf(command.command) == CmdClass::Draw) {
					list->EndQuery(samples_, D3D12_QUERY_TYPE_OCCLUSION, slot->second);
					drawn_.push_back(slot->second);
					drawnEids_.insert(command.eid);
				}
				if (counting_ && Counted(command)) {
					list->EndQuery(stats_, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot->second);
					counted_.push_back(slot->second);
				}
				list->EndQuery(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot->second * 2 + 1);
			}

			void EndList(ID3D12GraphicsCommandList* list, D3D12_COMMAND_LIST_TYPE type) override {
				for (u32 slot : used_) list->ResolveQueryData(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2, 2, timeResults_, (u64)slot * 16);
				for (u32 slot : counted_) list->ResolveQueryData(stats_, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot, 1, statResults_, (u64)slot * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
				for (u32 slot : drawn_) list->ResolveQueryData(samples_, D3D12_QUERY_TYPE_OCCLUSION, slot, 1, sampleResults_, (u64)slot * 8);
				used_.clear();
				counted_.clear();
				drawn_.clear();
				list_ = list;
				listType_ = type;
			}

			void SetQueue(u64 queue) { queue_ = queue; }
			// Statistics and samples on (the counting replay), or only timestamps.
			void Counting(bool on) { counting_ = on; }

			// Ticks of each action's begin and end, by eid.
			std::map<u32, std::pair<u64, u64>> Times() {
				std::map<u32, std::pair<u64, u64>> times;
				const u64* ticks = Map<u64>(timeResults_);
				if (!ticks) return times;
				for (const auto& slot : slots_) {
					const u64 begin = ticks[slot.second * 2], end = ticks[slot.second * 2 + 1];
					if (begin && end >= begin) times[slot.first] = {begin, end};
				}
				Unmap(timeResults_);
				return times;
			}

			std::map<u32, D3D12_QUERY_DATA_PIPELINE_STATISTICS> Stats() {
				std::map<u32, D3D12_QUERY_DATA_PIPELINE_STATISTICS> stats;
				const D3D12_QUERY_DATA_PIPELINE_STATISTICS* data = Map<D3D12_QUERY_DATA_PIPELINE_STATISTICS>(statResults_);
				if (!data) return stats;
				for (const auto& slot : slots_) {
					const D3D12_QUERY_DATA_PIPELINE_STATISTICS& s = data[slot.second];
					if (s.IAVertices || s.VSInvocations || s.PSInvocations || s.CSInvocations || s.CInvocations) stats[slot.first] = s;
				}
				Unmap(statResults_);
				return stats;
			}

			// The samples each draw's pixels passed the depth and stencil tests with, by eid.
			std::map<u32, u64> Samples() {
				std::map<u32, u64> samples;
				const u64* data = Map<u64>(sampleResults_);
				if (!data) return samples;
				for (const auto& slot : slots_)
					if (drawnEids_.count(slot.first)) samples[slot.first] = data[slot.second];
				Unmap(sampleResults_);
				return samples;
			}

			void Clear() {
				drawnEids_.clear();
				for (ID3D12Resource* buffer : {timeResults_, statResults_, sampleResults_}) {
					u8* mapped = nullptr;
					if (SUCCEEDED(buffer->Map(0, nullptr, (void**)&mapped))) {
						memset(mapped, 0, (size_t)buffer->GetDesc().Width);
						buffer->Unmap(0, nullptr);
					}
				}
			}

		private:
			static ID3D12Resource* Buffer(ID3D12Device* device, u64 bytes) {
				D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_READBACK};
				D3D12_RESOURCE_DESC desc = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, std::max<u64>(bytes, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
				ID3D12Resource* buffer = nullptr;
				if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&buffer)))
					Fail("cannot make the timing readback");
				return buffer;
			}
			template <class T> static const T* Map(ID3D12Resource* buffer) {
				void* mapped = nullptr;
				const D3D12_RANGE read = {0, (SIZE_T)buffer->GetDesc().Width};
				return SUCCEEDED(buffer->Map(0, &read, &mapped)) ? (const T*)mapped : nullptr;
			}
			static void Unmap(ID3D12Resource* buffer) {
				const D3D12_RANGE none = {0, 0};
				buffer->Unmap(0, &none);
			}
			bool Timed(ID3D12GraphicsCommandList* list) const { return list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE; }
			static bool Counted(const CommandRef& command) {
				const CmdClass c = ClassOf(command.command);
				return c == CmdClass::Draw || c == CmdClass::Dispatch;
			}

			ReplayDevice& device_;
			std::map<u32, u32> slots_;
			std::vector<u32> used_;
			std::vector<u32> counted_;
			std::vector<u32> drawn_;
			std::set<u32> drawnEids_;
			ID3D12QueryHeap* times_ = nullptr;
			ID3D12QueryHeap* stats_ = nullptr;
			ID3D12QueryHeap* samples_ = nullptr;
			ID3D12Resource* timeResults_ = nullptr;
			ID3D12Resource* statResults_ = nullptr;
			ID3D12Resource* sampleResults_ = nullptr;
			ID3D12GraphicsCommandList* list_ = nullptr;
			D3D12_COMMAND_LIST_TYPE listType_ = D3D12_COMMAND_LIST_TYPE_DIRECT;
			u64 queue_ = 0;
			bool counting_ = true;
		};
	}

	void D3D12Replay::Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) {
		ReplayDevice& device = Device();
		const u32 repeat = (u32)std::clamp<u64>(params["repeat"].Uint(5), 1, 25);
		const u32 last = (u32)model_.events.size();
		TimingHooks hooks(device, model_);
		// The direct queue's frequency converts ticks; queues of one device share a timestamp clock.
		u64 queue = 0;
		for (const auto& object : model_.objects)
			if (object.second.kind == ObjectKind::CommandQueue && device.QueueType(object.first) == D3D12_COMMAND_LIST_TYPE_DIRECT) {
				queue = object.first;
				break;
			}
		const double frequency = (double)device.TimestampFrequency(queue);
		std::vector<std::map<u32, std::pair<u64, u64>>> runs;
		std::map<u32, D3D12_QUERY_DATA_PIPELINE_STATISTICS> stats;
		std::map<u32, u64> samples;
		if (progress) progress("timing", 0.0);
		hooks.Clear();
		device.ReplayTo(last, &hooks);
		stats = hooks.Stats(), samples = hooks.Samples();
		hooks.Counting(false);
		for (u32 r = 0; r < repeat; ++r) {
			if (progress) progress("timing", (r + 1.0) / (repeat + 1.0));
			hooks.Clear();
			device.ReplayTo(last, &hooks);
			runs.push_back(hooks.Times());
		}
		replayedTo_ = UINT32_MAX;
		last_.valid = false;
		if (progress) progress("timing", 1.0);
		// The median frame (by its span) gives the markers' spans; each action's time is its own median.
		std::vector<std::pair<u64, size_t>> spans;
		for (size_t r = 0; r < runs.size(); ++r) {
			u64 low = UINT64_MAX, high = 0;
			for (const auto& t : runs[r]) {
				low = std::min(low, t.second.first);
				high = std::max(high, t.second.second);
			}
			spans.push_back({high > low ? high - low : 0, r});
		}
		std::sort(spans.begin(), spans.end());
		const std::map<u32, std::pair<u64, u64>>& median = runs[spans[spans.size() / 2].second];
		auto ms = [&](u64 ticks) { return frequency > 0 ? ticks * 1000.0 / frequency : 0.0; };
		out.Uint("repeat", repeat);
		out.Double("frameMs", ms(spans[spans.size() / 2].first));
		u64 origin = UINT64_MAX;
		for (const auto& t : median) origin = std::min(origin, t.second.first);
		out.BeginArray("columns");
		for (const char* column : {"eid", "ms", "start", "end"}) out.String(column);
		out.EndArray();
		out.BeginArray("events");
		for (const auto& t : median) {
			std::vector<u64> durations;
			for (const auto& run : runs) {
				auto found = run.find(t.first);
				if (found != run.end()) durations.push_back(found->second.second - found->second.first);
			}
			std::sort(durations.begin(), durations.end());
			out.BeginArray();
			out.Uint(t.first);
			out.Double(ms(durations[durations.size() / 2]));
			out.Double(ms(t.second.first - origin));
			out.Double(ms(t.second.second - origin));
			out.EndArray();
		}
		out.EndArray();
		out.BeginArray("markers");
		for (const Event& event : model_.events) {
			if (event.kind != EventKind::Marker || event.hidden || event.lastEid <= event.eid) continue;
			u64 low = UINT64_MAX, high = 0;
			for (auto it = median.lower_bound(event.eid); it != median.end() && it->first <= event.lastEid; ++it) {
				low = std::min(low, it->second.first);
				high = std::max(high, it->second.second);
			}
			if (high <= low) continue;
			out.BeginArray();
			out.Uint(event.eid);
			out.Double(ms(high - low));
			out.Double(ms(low - origin));
			out.Double(ms(high - origin));
			out.EndArray();
		}
		out.EndArray();
		out.BeginArray("statColumns");
		for (const char* column : {"eid", "iaVertices", "iaPrimitives", "vsInvocations", "gsInvocations", "gsPrimitives", "cInvocations", "cPrimitives", "psInvocations", "hsInvocations", "dsInvocations", "csInvocations", "samples"})
			out.String(column);
		out.EndArray();
		out.BeginArray("stats");
		for (const auto& s : stats) {
			const D3D12_QUERY_DATA_PIPELINE_STATISTICS& d = s.second;
			out.BeginArray();
			auto drawn = samples.find(s.first);
			for (u64 v : {(u64)s.first, d.IAVertices, d.IAPrimitives, d.VSInvocations, d.GSInvocations, d.GSPrimitives, d.CInvocations, d.CPrimitives, d.PSInvocations, d.HSInvocations, d.DSInvocations, d.CSInvocations,
					 drawn != samples.end() ? drawn->second : (u64)0})
				out.Uint(v);
			out.EndArray();
		}
		out.EndArray();
	}
}
