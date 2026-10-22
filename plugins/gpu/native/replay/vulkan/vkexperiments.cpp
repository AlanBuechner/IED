// Experiments for Vulkan (docs/22 "Performance"): a pass or draw timed as captured, then with each of its draws changed -
// a fragment shader writing zero to the same locations (assembled by Flashbang's own assembler), the scissor cut to one
// pixel (dynamically, or in a copy of a pipeline whose scissor is static), color writes off - each the median of
// `repeat` replays up to the range's end (common/experiments.h).
#include "vkreplay.h"
#include "vkspirv.h"

#include <algorithm>
#include <functional>

namespace fb::replay::vulkan
{
	namespace
	{
		// A draw of the range: what the captured frame binds for it, and what an experiment draws it with.
		struct ExperimentDraw
		{
			u64 pipeline = 0;
			std::vector<VkRect2D> scissors;     // set by vkCmdSetScissor (a dynamic scissor)
			bool dynamicScissor = false;
			VkPipeline variant = VK_NULL_HANDLE;
		};

		class ExperimentHooks final : public ReplayHooks
		{
		public:
			ExperimentHooks(ReplayDevice& device, u32 first, u32 last, u32 actions) : device_(device), first_(first), last_(last), count_(std::max<u32>(1, actions)) {
				VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
				info.queryType = VK_QUERY_TYPE_TIMESTAMP;
				info.queryCount = count_ * 2;
				if (device.vk.vkCreateQueryPool(device.device, &info, nullptr, &times_) != VK_SUCCESS) Fail("the experiments' timestamps could not be made");
			}
			~ExperimentHooks() override {
				if (times_) device_.vk.vkDestroyQueryPool(device_.device, times_, nullptr);
			}

			// The pool is reset in a submission of its own before each replay.
			void Start(Experiment experiment, std::map<u32, ExperimentDraw>& draws) {
				experiment_ = experiment;
				draws_ = &draws;
				order_.clear();
				VkCommandBuffer cb = device_.BeginOwn();
				device_.vk.vkCmdResetQueryPool(cb, times_, 0, count_ * 2);
				device_.SubmitOwn(cb);
			}

			void BeforeAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				if (command.eid < first_ || command.eid > last_ || order_.size() >= count_) return;
				const u32 index = (u32)order_.size();
				order_.push_back(command.eid);
				device_.vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, times_, index * 2);
				auto draw = draws_->find(command.eid);
				if (draw == draws_->end() || experiment_ == Experiment::Captured) return;
				const ExperimentDraw& d = draw->second;
				if (experiment_ == Experiment::OnePixel && d.dynamicScissor && !d.scissors.empty()) {
					const std::vector<VkRect2D> pixel(d.scissors.size(), VkRect2D{d.scissors[0].offset, {1, 1}});
					device_.vk.vkCmdSetScissor(cb, 0, (u32)pixel.size(), pixel.data());
				}
				else if (d.variant) device_.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, d.variant);
			}

			void AfterAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				if (order_.empty() || order_.back() != command.eid) return;
				auto draw = draws_->find(command.eid);
				if (draw != draws_->end() && experiment_ != Experiment::Captured) {
					const ExperimentDraw& d = draw->second;
					if (experiment_ == Experiment::OnePixel && d.dynamicScissor && !d.scissors.empty()) device_.vk.vkCmdSetScissor(cb, 0, (u32)d.scissors.size(), d.scissors.data());
					else if (d.variant) device_.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipeline)(uintptr_t)device_.Object(d.pipeline));
				}
				device_.vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, times_, ((u32)order_.size() - 1) * 2 + 1);
			}

			// The range's span in ticks: its first action's start to its last action's end.
			u64 Span() {
				if (order_.empty()) return 0;
				std::vector<u64> ticks(order_.size() * 2);
				device_.vk.vkGetQueryPoolResults(device_.device, times_, 0, (u32)ticks.size(), ticks.size() * 8, ticks.data(), 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
				u64 low = UINT64_MAX, high = 0;
				for (size_t i = 0; i < order_.size(); ++i) {
					low = std::min(low, ticks[i * 2]);
					high = std::max(high, ticks[i * 2 + 1]);
				}
				return high > low ? high - low : 0;
			}

		private:
			ReplayDevice& device_;
			u32 first_, last_, count_;
			VkQueryPool times_ = VK_NULL_HANDLE;
			Experiment experiment_ = Experiment::Captured;
			std::map<u32, ExperimentDraw>* draws_ = nullptr;
			std::vector<u32> order_;
		};

		// The outputs a fragment shader writes, by location; false when one is of a kind the trivial shader cannot write.
		bool TargetsOf(const std::vector<u32>& words, std::vector<TargetOutput>& out) {
			SpirvModule module;
			if (!ParseSpirv(words.data(), words.size(), module)) return false;
			for (const SpirvInterface& output : module.outputs) {
				if (!output.builtin.empty() || output.location < 0) continue;
				TargetOutput t;
				t.reg = (u32)output.location;
				const std::string& type = output.type;
				const size_t digits = type.find_first_of("0123456789");
				const std::string base = type.substr(0, digits);
				t.components = digits == std::string::npos ? 1 : (u32)atoi(type.c_str() + digits);
				if (base == "vec" || base == "float") t.kind = 0;
				else if (base == "ivec" || base == "int") t.kind = 1;
				else if (base == "uvec" || base == "uint") t.kind = 2;
				else return false;
				if ((base == "float" || base == "int" || base == "uint") && digits != std::string::npos) return false;   // 16 or 64 bits
				out.push_back(t);
			}
			return true;
		}

		bool HasDynamicScissor(const VkGraphicsPipelineCreateInfo& info) {
			for (u32 i = 0; info.pDynamicState && i < info.pDynamicState->dynamicStateCount; ++i)
				if (info.pDynamicState->pDynamicStates[i] == VK_DYNAMIC_STATE_SCISSOR || info.pDynamicState->pDynamicStates[i] == VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT) return true;
			return false;
		}

		// What experiments make, destroyed with them.
		class Variants
		{
		public:
			explicit Variants(ReplayDevice& device) : device_(device) {}
			~Variants() { Clear(); }

			void Clear() {
				for (auto& made : pipelines_)
					if (made.second) device_.vk.vkDestroyPipeline(device_.device, made.second, nullptr);
				for (VkShaderModule module : modules_) device_.vk.vkDestroyShaderModule(device_.device, module, nullptr);
				pipelines_.clear();
				modules_.clear();
			}

			// `pipeline`'s copy for the experiment, made once; null for a pipeline the experiment leaves as captured.
			VkPipeline For(const Model& model, u64 pipeline, Experiment experiment, const ExperimentDraw& draw, std::string& problem) {
				auto made = pipelines_.find(pipeline);
				if (made != pipelines_.end()) return made->second;
				return pipelines_[pipeline] = Make(model, pipeline, experiment, draw, problem);
			}

		private:
			VkPipeline Make(const Model& model, u64 pipeline, Experiment experiment, const ExperimentDraw& draw, std::string& problem) {
				const ObjectEntry* entry = model.Find(pipeline);
				if (!entry || entry->recordType != kGraphicsPipeline) return VK_NULL_HANDLE;
				Create<VkGraphicsPipelineCreateInfo> record = {};
				if (!model.Decode(entry->record, entry->recordSize, record, &device_)) return VK_NULL_HANDLE;
				VkGraphicsPipelineCreateInfo info = record.info;
				std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages, info.pStages + info.stageCount);
				VkPipelineColorBlendStateCreateInfo blend = info.pColorBlendState ? *info.pColorBlendState : VkPipelineColorBlendStateCreateInfo{};
				std::vector<VkPipelineColorBlendAttachmentState> attachments;
				VkPipelineViewportStateCreateInfo viewport = info.pViewportState ? *info.pViewportState : VkPipelineViewportStateCreateInfo{};
				std::vector<VkRect2D> scissors;
				if (experiment == Experiment::TrivialPixelShader) {
					Create<VkGraphicsPipelineCreateInfo> ids = {};
					model.DecodeObject(pipeline, ids);
					bool replaced = false;
					for (u32 i = 0; i < ids.info.stageCount; ++i) {
						if (ids.info.pStages[i].stage != VK_SHADER_STAGE_FRAGMENT_BIT) continue;
						Create<VkShaderModuleCreateInfo> module = {};
						std::vector<TargetOutput> targets;
						if (!model.DecodeObject(IdOf(ids.info.pStages[i].module), module) || !module.info.pCode ||
							!TargetsOf(std::vector<u32>(module.info.pCode, module.info.pCode + module.info.codeSize / 4), targets)) {
							problem = "a fragment shader writes outputs the trivial shader cannot";
							return VK_NULL_HANDLE;
						}
						std::vector<u32> words;
						std::string log;
						VkShaderModuleCreateInfo create = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
						VkShaderModule trivial = VK_NULL_HANDLE;
						if (!AssembleSpirv(TrivialFragmentShader(targets), nullptr, 0, words, log) ||
							(create.codeSize = words.size() * 4, create.pCode = words.data(), device_.vk.vkCreateShaderModule(device_.device, &create, nullptr, &trivial)) != VK_SUCCESS) {
							problem = "the trivial fragment shader could not be made: " + log;
							return VK_NULL_HANDLE;
						}
						modules_.push_back(trivial);
						stages[i].module = trivial;
						stages[i].pName = "main";
						stages[i].pSpecializationInfo = nullptr;
						replaced = true;
					}
					if (!replaced) return VK_NULL_HANDLE;    // no fragment shader: nothing to make trivial
					info.pStages = stages.data();
				}
				else if (experiment == Experiment::NoColorWrites) {
					if (!info.pColorBlendState) return VK_NULL_HANDLE;
					attachments.assign(blend.pAttachments, blend.pAttachments + blend.attachmentCount);
					for (VkPipelineColorBlendAttachmentState& a : attachments) {
						a.colorWriteMask = 0;
						a.blendEnable = VK_FALSE;
					}
					blend.pAttachments = attachments.data();
					blend.logicOpEnable = VK_FALSE;
					info.pColorBlendState = &blend;
				}
				else {
					// One pixel with a static scissor: a copy whose scissors are each that pixel.
					if (draw.dynamicScissor || !info.pViewportState || !viewport.pScissors) return VK_NULL_HANDLE;
					scissors.assign(viewport.scissorCount, VkRect2D{viewport.pScissors[0].offset, {1, 1}});
					viewport.pScissors = scissors.data();
					info.pViewportState = &viewport;
				}
				info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
				info.basePipelineHandle = VK_NULL_HANDLE;
				info.basePipelineIndex = -1;
				VkPipeline made = VK_NULL_HANDLE;
				if (device_.vk.vkCreateGraphicsPipelines(device_.device, VK_NULL_HANDLE, 1, &info, nullptr, &made) != VK_SUCCESS) {
					problem = "pipeline " + model.NameOf(pipeline) + " could not be made for the experiment";
					return VK_NULL_HANDLE;
				}
				return made;
			}

			ReplayDevice& device_;
			std::map<u64, VkPipeline> pipelines_;
			std::vector<VkShaderModule> modules_;
		};
	}

	void VulkanReplay::Experiments(const JsonValue& params, JsonWriter& out, const Progress& progress) {
		ReplayDevice& device = Device();
		const u32 eid = (u32)params["eid"].Uint(0);
		const Event* event = model_.EventAt(eid);
		if (!event) Fail("there is no event %u", eid);
		const u32 first = eid, last = event->kind == EventKind::Marker ? std::max(eid, event->lastEid) : eid;
		const u32 repeat = (u32)std::clamp<u64>(params["repeat"].Uint(5), 1, 25);
		std::map<u32, ExperimentDraw> draws;
		u32 actions = 0, dispatches = 0;
		for (u32 at = first; at <= last; ++at) {
			const Event* e = model_.EventAt(at);
			if (!e || (e->kind != EventKind::Draw && e->kind != EventKind::Dispatch && e->kind != EventKind::Copy && e->kind != EventKind::Clear && e->kind != EventKind::Resolve)) continue;
			++actions;
			if (e->kind == EventKind::Dispatch) ++dispatches;
			if (e->kind != EventKind::Draw) continue;
			const Snapshot s = Simulate(model_, at);
			ExperimentDraw draw;
			draw.pipeline = s.graphicsPipeline;
			draw.scissors = s.scissors;
			Create<VkGraphicsPipelineCreateInfo> record = {};
			draw.dynamicScissor = model_.DecodeObject(s.graphicsPipeline, record) && HasDynamicScissor(record.info);
			draws[at] = draw;
		}
		if (!actions) Fail("event %u holds no action to time", eid);
		const double period = device.properties.limits.timestampPeriod;   // nanoseconds per tick
		ExperimentHooks hooks(device, first, last, actions);
		ExperimentRun runs[kExperiments];
		for (u32 e = 0; e < kExperiments; ++e) {
			const Experiment experiment = (Experiment)e;
			Variants variants(device);
			for (auto& draw : draws) {
				draw.second.variant = VK_NULL_HANDLE;
				if (experiment == Experiment::Captured) continue;
				if (experiment == Experiment::OnePixel && draw.second.dynamicScissor && !draw.second.scissors.empty()) {
					++runs[e].changed;
					continue;
				}
				draw.second.variant = variants.For(model_, draw.second.pipeline, experiment, draw.second, runs[e].problem);
				runs[e].changed += draw.second.variant ? 1 : 0;
			}
			if (experiment == Experiment::Captured || runs[e].changed) {
				std::vector<u64> spans;
				for (u32 r = 0; r < repeat; ++r) {
					if (progress) progress("experiments", (e * repeat + r) / (double)(kExperiments * repeat));
					hooks.Start(experiment, draws);
					device.ReplayTo(last, &hooks);
					spans.push_back(hooks.Span());
				}
				std::sort(spans.begin(), spans.end());
				runs[e].ms = spans[spans.size() / 2] * period / 1e6;
			}
			for (auto& draw : draws) draw.second.variant = VK_NULL_HANDLE;
		}
		Unreplayed();
		if (progress) progress("experiments", 1.0);
		out.Uint("eid", eid).Uint("first", first).Uint("last", last).Uint("draws", draws.size()).Uint("dispatches", dispatches).Uint("repeat", repeat);
		WriteExperiments(out, runs, LimitsOf(runs));
	}
}
