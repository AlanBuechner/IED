// Timing (F-GPU-12): the frame replayed with a timestamp before and after every action, and a pipeline-statistics query
// around it where the device has them; the median of the replays per action, markers spanning their actions. The
// statistics' eleven counters are D3D12's columns in the same order.
#include "vkreplay.h"

#include <algorithm>

namespace fb::replay::vulkan
{
	namespace
	{
		class TimingHooks : public ReplayHooks
		{
		public:
			TimingHooks(ReplayDevice& device, u32 actions) : device_(device), actions_(actions) {
				VkQueryPoolCreateInfo times = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
				times.queryType = VK_QUERY_TYPE_TIMESTAMP;
				times.queryCount = std::max<u32>(1, actions * 2);
				if (device.vk.vkCreateQueryPool(device.device, &times, nullptr, &times_) != VK_SUCCESS) Fail("the timing queries could not be made");
				if (device.pipelineStatistics) {
					VkQueryPoolCreateInfo stats = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
					stats.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
					stats.queryCount = std::max<u32>(1, actions);
					stats.pipelineStatistics = 0x7FF;   // the eleven counters of Vulkan 1.0
					if (device.vk.vkCreateQueryPool(device.device, &stats, nullptr, &stats_) != VK_SUCCESS) stats_ = VK_NULL_HANDLE;
				}
			}
			~TimingHooks() override {
				if (times_) device_.vk.vkDestroyQueryPool(device_.device, times_, nullptr);
				if (stats_) device_.vk.vkDestroyQueryPool(device_.device, stats_, nullptr);
			}

			// The pools are reset in a submission of their own, before the replay (a reset inside each submission would
			// lose the ones before it).
			void Clear() {
				order_.clear();
				VkCommandBuffer cb = device_.BeginOwn();
				device_.vk.vkCmdResetQueryPool(cb, times_, 0, std::max<u32>(1, actions_ * 2));
				if (stats_) device_.vk.vkCmdResetQueryPool(cb, stats_, 0, std::max<u32>(1, actions_));
				device_.SubmitOwn(cb);
			}

			void BeforeAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				const u32 index = (u32)order_.size();
				if (index >= actions_) return;
				order_.push_back(command.eid);
				device_.vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, times_, index * 2);
				if (stats_) device_.vk.vkCmdBeginQuery(cb, stats_, index, 0);
			}

			void AfterAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				if (order_.empty() || order_.back() != command.eid) return;
				const u32 index = (u32)order_.size() - 1;
				if (stats_) device_.vk.vkCmdEndQuery(cb, stats_, index);
				device_.vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, times_, index * 2 + 1);
			}

			// eid -> (start, end) in ticks.
			std::map<u32, std::pair<u64, u64>> Times() {
				std::map<u32, std::pair<u64, u64>> out;
				if (order_.empty()) return out;
				std::vector<u64> ticks(order_.size() * 2);
				device_.vk.vkGetQueryPoolResults(device_.device, times_, 0, (u32)ticks.size(), ticks.size() * 8, ticks.data(), 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
				for (size_t i = 0; i < order_.size(); ++i) out[order_[i]] = {ticks[i * 2], ticks[i * 2 + 1]};
				return out;
			}

			std::map<u32, std::vector<u64>> Stats() {
				std::map<u32, std::vector<u64>> out;
				if (!stats_ || order_.empty()) return out;
				std::vector<u64> values(order_.size() * 11);
				device_.vk.vkGetQueryPoolResults(device_.device, stats_, 0, (u32)order_.size(), values.size() * 8, values.data(), 11 * 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
				for (size_t i = 0; i < order_.size(); ++i) out[order_[i]] = std::vector<u64>(values.begin() + i * 11, values.begin() + (i + 1) * 11);
				return out;
			}

		private:
			ReplayDevice& device_;
			u32 actions_;
			VkQueryPool times_ = VK_NULL_HANDLE;
			VkQueryPool stats_ = VK_NULL_HANDLE;
			std::vector<u32> order_;
		};
	}

	void VulkanReplay::Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) {
		ReplayDevice& device = Device();
		const u32 repeat = std::clamp<u32>((u32)params["repeat"].Uint(5), 1, 25);
		u32 actions = 0;
		for (const FrameOp& op : model_.frame)
			for (const BufferRef& buffer : op.buffers)
				for (const CommandRef& command : buffer.commands)
					if (IsAction(command.command)) ++actions;
		TimingHooks hooks(device, actions);
		std::vector<std::map<u32, std::pair<u64, u64>>> runs;
		std::map<u32, std::vector<u64>> stats;
		for (u32 r = 0; r < repeat; ++r) {
			if (progress) progress("timing", (double)r / repeat);
			hooks.Clear();
			device.ReplayTo(UINT32_MAX, &hooks);
			runs.push_back(hooks.Times());
			if (r == 0) stats = hooks.Stats();
		}
		replayed_ = false;
		last_.valid = false;
		if (progress) progress("timing", 1.0);
		const double period = device.properties.limits.timestampPeriod;   // nanoseconds per tick
		auto ms = [&](u64 ticks) { return ticks * period / 1e6; };
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
				if (found != run.end()) durations.push_back(found->second.second > found->second.first ? found->second.second - found->second.first : 0);
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
		for (const char* column : {"eid", "iaVertices", "iaPrimitives", "vsInvocations", "gsInvocations", "gsPrimitives", "cInvocations", "cPrimitives", "psInvocations", "hsInvocations", "dsInvocations", "csInvocations"})
			out.String(column);
		out.EndArray();
		out.BeginArray("stats");
		for (const auto& s : stats) {
			out.BeginArray();
			out.Uint(s.first);
			for (u64 v : s.second) out.Uint(v);
			out.EndArray();
		}
		out.EndArray();
	}
}
