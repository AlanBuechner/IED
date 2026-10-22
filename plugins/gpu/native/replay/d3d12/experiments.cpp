// Experiments for D3D12 (docs/22 "Performance"): a pass or draw timed as captured, then with each of its draws changed -
// a pixel shader writing zero to the same targets (compiled as the captured one was: DXBC or DXIL), the scissor cut to
// one pixel, color writes off - each the median of `repeat` replays up to the range's end (common/experiments.h).
#include "d3d12replay.h"
#include "dxbc.h"
#include "experiments.h"
#include "hlsl.h"

#include <algorithm>
#include <set>

namespace fb::replay::d3d12
{
	namespace
	{
		constexpr u32 kTargetSystemValue = 64;   // D3D_NAME_TARGET

		// A draw of the range: what the captured frame binds for it, and what an experiment draws it with.
		struct ExperimentDraw
		{
			u64 pipeline = 0;
			std::vector<D3D12_RECT> scissors;
			ID3D12PipelineState* variant = nullptr;
		};

		class ExperimentHooks final : public ReplayHooks
		{
		public:
			ExperimentHooks(ReplayDevice& device, u32 first, u32 last, u32 actions) : device_(device), first_(first), last_(last), count_(std::max<u32>(1, actions)) {
				D3D12_QUERY_HEAP_DESC times = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, count_ * 2};
				if (FAILED(device.Device()->CreateQueryHeap(&times, __uuidof(ID3D12QueryHeap), (void**)&times_))) Fail("cannot make the experiments' timestamps");
				results_ = MakeReadback(device.Device(), (u64)count_ * 16);
			}
			~ExperimentHooks() {
				if (times_) times_->Release();
				if (results_) results_->Release();
			}

			void Start(Experiment experiment, std::map<u32, ExperimentDraw>& draws) {
				experiment_ = experiment;
				draws_ = &draws;
				slots_.clear();
				resolved_.clear();
				u8* mapped = nullptr;
				if (SUCCEEDED(results_->Map(0, nullptr, (void**)&mapped))) {
					memset(mapped, 0, (size_t)count_ * 16);
					results_->Unmap(0, nullptr);
				}
			}

			void Before(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				if (command.eid < first_ || command.eid > last_ || !IsAction(command.command) || !Timed(list) || slots_.size() >= count_) return;
				const u32 slot = (u32)slots_.size();
				slots_[command.eid] = slot;
				list->EndQuery(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
				auto draw = draws_->find(command.eid);
				if (draw == draws_->end() || experiment_ == Experiment::Captured) return;
				if (experiment_ == Experiment::OnePixel) {
					const UINT count = (UINT)std::max<size_t>(1, draw->second.scissors.size());
					const LONG x = draw->second.scissors.empty() ? 0 : draw->second.scissors[0].left, y = draw->second.scissors.empty() ? 0 : draw->second.scissors[0].top;
					const std::vector<D3D12_RECT> pixel(count, D3D12_RECT{x, y, x + 1, y + 1});
					list->RSSetScissorRects(count, pixel.data());
				}
				else if (draw->second.variant) list->SetPipelineState(draw->second.variant);
			}

			void After(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				auto slot = slots_.find(command.eid);
				if (slot == slots_.end()) return;
				auto draw = draws_->find(command.eid);
				if (draw != draws_->end() && experiment_ == Experiment::OnePixel) {
					const std::vector<D3D12_RECT>& scissors = draw->second.scissors;
					const D3D12_RECT everything = {0, 0, D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX};
					list->RSSetScissorRects(scissors.empty() ? 1 : (UINT)scissors.size(), scissors.empty() ? &everything : scissors.data());
				}
				else if (draw != draws_->end() && draw->second.variant && experiment_ != Experiment::Captured)
					list->SetPipelineState((ID3D12PipelineState*)device_.Object(draw->second.pipeline));
				list->EndQuery(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot->second * 2 + 1);
			}

			void EndList(ID3D12GraphicsCommandList* list, D3D12_COMMAND_LIST_TYPE type) override {
				for (const auto& slot : slots_)
					if (!resolved_.count(slot.second)) {
						list->ResolveQueryData(times_, D3D12_QUERY_TYPE_TIMESTAMP, slot.second * 2, 2, results_, (u64)slot.second * 16);
						resolved_.insert(slot.second);
					}
			}

			// The range's span in ticks: its first action's start to its last action's end.
			u64 Span() {
				const u64* ticks = nullptr;
				const D3D12_RANGE read = {0, (SIZE_T)count_ * 16};
				if (FAILED(results_->Map(0, &read, (void**)&ticks))) return 0;
				u64 low = UINT64_MAX, high = 0;
				for (const auto& slot : slots_) {
					const u64 begin = ticks[slot.second * 2], end = ticks[slot.second * 2 + 1];
					if (!begin || end < begin) continue;
					low = std::min(low, begin);
					high = std::max(high, end);
				}
				const D3D12_RANGE none = {0, 0};
				results_->Unmap(0, &none);
				return high > low ? high - low : 0;
			}

		private:
			static bool Timed(ID3D12GraphicsCommandList* list) { return list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT || list->GetType() == D3D12_COMMAND_LIST_TYPE_COMPUTE; }

			ReplayDevice& device_;
			u32 first_, last_, count_;
			ID3D12QueryHeap* times_ = nullptr;
			ID3D12Resource* results_ = nullptr;
			Experiment experiment_ = Experiment::Captured;
			std::map<u32, ExperimentDraw>* draws_ = nullptr;
			std::map<u32, u32> slots_;
			std::set<u32> resolved_;
		};

		// The render targets a pixel shader writes, from its output signature.
		std::vector<TargetOutput> TargetsOf(const D3D12_SHADER_BYTECODE& code) {
			std::vector<TargetOutput> targets;
			std::vector<DxbcElement> elements;
			ReadDxbcSignature((const u8*)code.pShaderBytecode, code.BytecodeLength, 'O', elements);
			for (const DxbcElement& e : elements) {
				if (e.system != kTargetSystemValue && e.name != "SV_Target" && e.name != "SV_TARGET") continue;
				u32 components = 0;
				for (u32 k = 0; k < 4; ++k) components += e.mask >> k & 1;
				targets.push_back({e.reg, std::max<u32>(1, components), e.component == 1 ? 2u : e.component == 2 ? 1u : 0u});
			}
			return targets;
		}

		// The pipeline state an experiment draws with, made from the captured one; null (with `problem` when it should
		// have been made) for a draw the experiment leaves as captured.
		ID3D12PipelineState* MakeVariant(ReplayDevice& device, const Model& model, u64 pipeline, Experiment experiment, std::map<std::string, std::vector<u8>>& compiled, std::string& problem) {
			if (experiment == Experiment::NoColorWrites) {
				ID3D12PipelineState* made = device.Variant(pipeline, [](PipelineCreate& p) {
					for (D3D12_RENDER_TARGET_BLEND_DESC& target : p.blend.RenderTarget) {
						target.RenderTargetWriteMask = 0;
						target.BlendEnable = FALSE;
						target.LogicOpEnable = FALSE;
					}
				});
				if (!made) problem = "pipeline state " + model.NameOf(pipeline) + " could not be made with color writes off";
				return made;
			}
			const ObjectEntry* entry = model.Find(pipeline);
			PipelineCreate captured = {};
			if (!entry || !model.DecodeModel(entry->record, entry->recordSize, captured) || !captured.ps.pShaderBytecode) return nullptr;
			const bool dxil = IsDxil(captured.ps);
			const std::string source = TrivialPixelShaderHlsl(TargetsOf(captured.ps));
			const std::string key = (dxil ? "dxil:" : "dxbc:") + source;
			auto code = compiled.find(key);
			if (code == compiled.end()) {
				std::vector<u8> bytes;
				std::string log;
				if (!CompileHlsl(source, "main", dxil ? "ps_6_0" : "ps_5_0", dxil ? HlslTarget::Dxil : HlslTarget::Dxbc, bytes, log)) {
					problem = "the trivial pixel shader did not compile: " + log;
					return nullptr;
				}
				code = compiled.emplace(key, bytes).first;
			}
			const std::vector<u8>& bytes = code->second;
			ID3D12PipelineState* made = device.Variant(pipeline, [&](PipelineCreate& p) { p.ps = {bytes.data(), bytes.size()}; });
			if (!made) problem = "pipeline state " + model.NameOf(pipeline) + " could not be made with a trivial pixel shader";
			return made;
		}
	}

	void D3D12Replay::Experiments(const JsonValue& params, JsonWriter& out, const Progress& progress) {
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
			draws[at] = {s.pipeline, s.scissors, nullptr};
		}
		if (!actions) Fail("event %u holds no action to time", eid);
		u64 queue = 0;
		for (const auto& object : model_.objects)
			if (object.second.kind == ObjectKind::CommandQueue && device.QueueType(object.first) == D3D12_COMMAND_LIST_TYPE_DIRECT) {
				queue = object.first;
				break;
			}
		const double frequency = (double)device.TimestampFrequency(queue);
		ExperimentHooks hooks(device, first, last, actions);
		ExperimentRun runs[kExperiments];
		std::map<std::string, std::vector<u8>> compiled;
		for (u32 e = 0; e < kExperiments; ++e) {
			const Experiment experiment = (Experiment)e;
			// Each experiment's states, one per pipeline.
			std::map<u64, ID3D12PipelineState*> variants;
			if (experiment == Experiment::TrivialPixelShader || experiment == Experiment::NoColorWrites)
				for (auto& draw : draws) {
					auto made = variants.find(draw.second.pipeline);
					if (made == variants.end()) made = variants.emplace(draw.second.pipeline, MakeVariant(device, model_, draw.second.pipeline, experiment, compiled, runs[e].problem)).first;
					draw.second.variant = made->second;
					runs[e].changed += made->second ? 1 : 0;
				}
			else if (experiment == Experiment::OnePixel) runs[e].changed = (u32)draws.size();
			if (experiment == Experiment::Captured || runs[e].changed) {
				std::vector<u64> spans;
				for (u32 r = 0; r < repeat; ++r) {
					if (progress) progress("experiments", (e * repeat + r) / (double)(kExperiments * repeat));
					hooks.Start(experiment, draws);
					device.ReplayTo(last, &hooks);
					spans.push_back(hooks.Span());
				}
				std::sort(spans.begin(), spans.end());
				runs[e].ms = frequency > 0 ? spans[spans.size() / 2] * 1000.0 / frequency : 0;
			}
			for (auto& variant : variants)
				if (variant.second) variant.second->Release();
			for (auto& draw : draws) draw.second.variant = nullptr;
		}
		Unreplayed();
		if (progress) progress("experiments", 1.0);
		out.Uint("eid", eid).Uint("first", first).Uint("last", last).Uint("draws", draws.size()).Uint("dispatches", dispatches).Uint("repeat", repeat);
		WriteExperiments(out, runs, LimitsOf(runs));
	}
}
