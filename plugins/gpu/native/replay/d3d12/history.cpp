// Pixel History for D3D12 (docs/22 "Pixel History", common/pixelhistory.h): the actions up to an event that may write
// the pixel's subresource - draws with it bound as a render or depth target, clears, copies, resolves and discards into
// it, dispatches when it allows unordered access - then one replay that runs each such draw with its scissor clamped
// to the pixel inside an occlusion query, after copies of it with every write off that tell coverage, culling, depth
// and stencil apart. The clamped draws leave the rest of the frame unwritten, so the replay is forgotten after it.
#include "d3d12replay.h"
#include "pixelhistory.h"

#include <algorithm>
#include <array>
#include <set>

namespace fb::replay::d3d12
{
	namespace
	{
		enum Probe : u32 { kCovered, kFacing, kDepth, kPassed, kProbes };

		struct Spot
		{
			u64 id = 0;
			u32 subresource = 0;
			u32 mip = 0;
			u32 slice = 0;
			u32 x = 0;
			u32 y = 0;
		};

		// What the replay needs of a candidate draw.
		struct DrawProbe
		{
			u32 slot = 0;
			u64 pipeline = 0;
			std::vector<D3D12_RECT> scissors;   // the draw's own
			bool variants = false;
		};

		bool InRange(u32 value, u32 first, u32 count) { return value >= first && (count == UINT_MAX || value < first + count); }

		// Whether a render-target or depth view writes the spot's mip and slice.
		bool ViewCovers(const DescriptorWrite& view, const Spot& spot) {
			if (!view.hasDesc) return spot.mip == 0;
			if (view.kind == ViewKind::Rtv) {
				const D3D12_RENDER_TARGET_VIEW_DESC& d = view.rtv;
				switch (d.ViewDimension) {
					case D3D12_RTV_DIMENSION_TEXTURE1D: return spot.mip == d.Texture1D.MipSlice;
					case D3D12_RTV_DIMENSION_TEXTURE1DARRAY: return spot.mip == d.Texture1DArray.MipSlice && InRange(spot.slice, d.Texture1DArray.FirstArraySlice, d.Texture1DArray.ArraySize);
					case D3D12_RTV_DIMENSION_TEXTURE2D: return spot.mip == d.Texture2D.MipSlice;
					case D3D12_RTV_DIMENSION_TEXTURE2DARRAY: return spot.mip == d.Texture2DArray.MipSlice && InRange(spot.slice, d.Texture2DArray.FirstArraySlice, d.Texture2DArray.ArraySize);
					case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY: return InRange(spot.slice, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize);
					case D3D12_RTV_DIMENSION_TEXTURE3D: return spot.mip == d.Texture3D.MipSlice && InRange(spot.slice, d.Texture3D.FirstWSlice, d.Texture3D.WSize);
					default: return spot.mip == 0;
				}
			}
			const D3D12_DEPTH_STENCIL_VIEW_DESC& d = view.dsv;
			switch (d.ViewDimension) {
				case D3D12_DSV_DIMENSION_TEXTURE1D: return spot.mip == d.Texture1D.MipSlice;
				case D3D12_DSV_DIMENSION_TEXTURE1DARRAY: return spot.mip == d.Texture1DArray.MipSlice && InRange(spot.slice, d.Texture1DArray.FirstArraySlice, d.Texture1DArray.ArraySize);
				case D3D12_DSV_DIMENSION_TEXTURE2D: return spot.mip == d.Texture2D.MipSlice;
				case D3D12_DSV_DIMENSION_TEXTURE2DARRAY: return spot.mip == d.Texture2DArray.MipSlice && InRange(spot.slice, d.Texture2DArray.FirstArraySlice, d.Texture2DArray.ArraySize);
				case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY: return InRange(spot.slice, d.Texture2DMSArray.FirstArraySlice, d.Texture2DMSArray.ArraySize);
				default: return spot.mip == 0;
			}
		}

		// Whether the descriptor `tagged` names a view of the spot at the snapshot's point.
		bool Names(const Model& model, const Snapshot& s, u64 tagged, const Spot& spot) {
			DescriptorWrite view;
			return tagged && DescriptorAt(model, s, DescriptorHeapId(tagged), DescriptorIndex(tagged), view) && IdOf(view.resource) == spot.id && ViewCovers(view, spot);
		}

		bool InRects(const Spot& spot, UINT count, const D3D12_RECT* rects) {
			if (!count || !rects) return true;
			for (UINT i = 0; i < count; ++i)
				if (Inside(spot.x, spot.y, rects[i].left, rects[i].top, rects[i].right, rects[i].bottom)) return true;
			return false;
		}

		// Whether a clear, copy, resolve or discard (`raw`, its decoded arguments) writes the spot.
		bool WritesSpot(const Model& model, const CommandRef& command, void* raw, const Spot& spot) {
			switch (command.command) {
				case Cmd::ClearRenderTargetView: {
					auto& a = *(ClearRenderTargetArgs*)raw;
					return Names(model, Simulate(model, command.eid), a.view.ptr, spot) && InRects(spot, a.rectCount, a.rects);
				}
				case Cmd::ClearDepthStencilView: {
					auto& a = *(ClearDepthStencilArgs*)raw;
					return Names(model, Simulate(model, command.eid), a.view.ptr, spot) && InRects(spot, a.rectCount, a.rects);
				}
				case Cmd::ClearUnorderedAccessViewUint: {
					auto& a = *(ClearUavUintArgs*)raw;
					return IdOf(a.resource) == spot.id && InRects(spot, a.rectCount, a.rects);
				}
				case Cmd::ClearUnorderedAccessViewFloat: {
					auto& a = *(ClearUavFloatArgs*)raw;
					return IdOf(a.resource) == spot.id && InRects(spot, a.rectCount, a.rects);
				}
				case Cmd::CopyResource: return IdOf(((CopyResourceArgs*)raw)->dst) == spot.id;
				case Cmd::CopyTextureRegion: {
					auto& a = *(CopyTextureRegionArgs*)raw;
					if (IdOf(a.dst.pResource) != spot.id || (a.dst.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX && a.dst.SubresourceIndex != spot.subresource)) return false;
					if (spot.x < a.dstX || spot.y < a.dstY) return false;
					return !a.srcBox || (spot.x < a.dstX + (a.srcBox->right - a.srcBox->left) && spot.y < a.dstY + (a.srcBox->bottom - a.srcBox->top));
				}
				case Cmd::ResolveSubresource: {
					auto& a = *(ResolveSubresourceArgs*)raw;
					return IdOf(a.dst) == spot.id && a.dstSubresource == spot.subresource;
				}
				case Cmd::ResolveSubresourceRegion: {
					auto& a = *(ResolveRegionArgs*)raw;
					if (IdOf(a.dst) != spot.id || a.dstSubresource != spot.subresource || spot.x < a.dstX || spot.y < a.dstY) return false;
					return !a.srcRect || (spot.x < a.dstX + (UINT)(a.srcRect->right - a.srcRect->left) && spot.y < a.dstY + (UINT)(a.srcRect->bottom - a.srcRect->top));
				}
				case Cmd::DiscardResource: return IdOf(((DiscardArgs*)raw)->resource) == spot.id;
				case Cmd::CopyTiles: return IdOf(((CopyTilesArgs*)raw)->tiled) == spot.id;
				default: return false;
			}
		}

		// The pipeline as the copies of a draw run it: nothing written, and per probe the tests it keeps.
		void ProbeChange(Probe probe, PipelineCreate& p) {
			for (D3D12_RENDER_TARGET_BLEND_DESC& target : p.blend.RenderTarget) target.RenderTargetWriteMask = 0;
			p.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			p.depthStencil.FrontFace.StencilWriteMask = 0;
			p.depthStencil.BackFace.StencilWriteMask = 0;
			p.depthStencil.StencilEnable = FALSE;
			p.depthStencil.DepthBoundsTestEnable = FALSE;
			p.streamOutput = {};
			if (probe != kDepth) p.depthStencil.DepthEnable = FALSE;
			if (probe == kCovered) p.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
		}

		class ProbeHooks final : public ReplayHooks
		{
		public:
			ProbeHooks(ReplayDevice& device, const Model& model, std::map<u32, DrawProbe>& probes, const Spot& spot) : device_(device), probes_(probes) {
				const D3D12_RECT pixel = {(LONG)spot.x, (LONG)spot.y, (LONG)spot.x + 1, (LONG)spot.y + 1};
				pixel_ = pixel;
				const u32 count = std::max<u32>(1, (u32)probes.size()) * kProbes;
				D3D12_QUERY_HEAP_DESC desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, count};
				if (FAILED(device.Device()->CreateQueryHeap(&desc, __uuidof(ID3D12QueryHeap), (void**)&heap_))) Fail("cannot make the pixel history's occlusion queries");
				D3D12_HEAP_PROPERTIES readback = {D3D12_HEAP_TYPE_READBACK};
				D3D12_RESOURCE_DESC buffer = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, std::max<u64>((u64)count * 8, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
				if (FAILED(device.Device()->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&results_)))
					Fail("cannot make the pixel history's readback");
				for (auto& probe : probes) {
					const u64 pipeline = probe.second.pipeline;
					if (!variants_.count(pipeline)) variants_[pipeline] = MakeVariants(model, pipeline);
					probe.second.variants = variants_[pipeline][0] != nullptr;
				}
			}

			~ProbeHooks() {
				for (auto& variants : variants_)
					for (ID3D12PipelineState* variant : variants.second)
						if (variant) variant->Release();
				if (heap_) heap_->Release();
				if (results_) results_->Release();
			}

			void Before(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				auto found = probes_.find(command.eid);
				if (found == probes_.end()) return;
				const DrawProbe& probe = found->second;
				auto* original = (ID3D12PipelineState*)device_.Object(probe.pipeline);
				if (!original) return;
				const UINT count = (UINT)std::max<size_t>(1, probe.scissors.size());
				std::vector<D3D12_RECT> clamped(count), pixel(count, pixel_);
				for (UINT i = 0; i < count; ++i) clamped[i] = i < probe.scissors.size() ? Clamp(probe.scissors[i]) : pixel_;
				if (probe.variants) {
					const std::array<ID3D12PipelineState*, 3>& variants = variants_[probe.pipeline];
					for (u32 k = kCovered; k < kPassed; ++k) {
						list->SetPipelineState(variants[k]);
						list->RSSetScissorRects(count, k == kCovered ? pixel.data() : clamped.data());
						list->BeginQuery(heap_, D3D12_QUERY_TYPE_OCCLUSION, probe.slot * kProbes + k);
						device_.Repeat(list, command);
						list->EndQuery(heap_, D3D12_QUERY_TYPE_OCCLUSION, probe.slot * kProbes + k);
					}
					list->SetPipelineState(original);
				}
				list->RSSetScissorRects(count, clamped.data());
				list->BeginQuery(heap_, D3D12_QUERY_TYPE_OCCLUSION, probe.slot * kProbes + kPassed);
				open_ = command.eid;
			}

			void After(ID3D12GraphicsCommandList* list, const CommandRef& command) override {
				if (open_ != command.eid) return;
				const DrawProbe& probe = probes_.at(command.eid);
				list->EndQuery(heap_, D3D12_QUERY_TYPE_OCCLUSION, probe.slot * kProbes + kPassed);
				if (!probe.scissors.empty()) list->RSSetScissorRects((UINT)probe.scissors.size(), probe.scissors.data());
				used_.push_back(probe.slot);
				ran_.insert(command.eid);
				open_ = 0;
			}

			void EndList(ID3D12GraphicsCommandList* list, D3D12_COMMAND_LIST_TYPE type) override {
				for (u32 slot : used_) list->ResolveQueryData(heap_, D3D12_QUERY_TYPE_OCCLUSION, slot * kProbes, kProbes, results_, (u64)slot * kProbes * 8);
				used_.clear();
			}

			// The probes' samples, by eid; a draw the replay did not reach has none.
			void Results(std::vector<HistoryCandidate>& candidates) {
				void* mapped = nullptr;
				const D3D12_RANGE read = {0, (SIZE_T)results_->GetDesc().Width};
				if (FAILED(results_->Map(0, &read, &mapped))) return;
				const u64* samples = (const u64*)mapped;
				for (HistoryCandidate& candidate : candidates) {
					auto probe = probes_.find(candidate.eid);
					if (probe == probes_.end() || !ran_.count(candidate.eid)) continue;
					const u64* at = samples + (size_t)probe->second.slot * kProbes;
					DrawTests& tests = candidate.tests;
					tests.measured = true;
					tests.variants = probe->second.variants;
					tests.covered = at[kCovered];
					tests.facing = at[kFacing];
					tests.depth = at[kDepth];
					tests.passed = at[kPassed];
				}
				const D3D12_RANGE none = {0, 0};
				results_->Unmap(0, &none);
			}

		private:
			D3D12_RECT Clamp(const D3D12_RECT& rect) const {
				D3D12_RECT out = {std::max(rect.left, pixel_.left), std::max(rect.top, pixel_.top), std::min(rect.right, pixel_.right), std::min(rect.bottom, pixel_.bottom)};
				if (out.left >= out.right || out.top >= out.bottom) out = {0, 0, 0, 0};
				return out;
			}

			// The covered, facing and depth copies of a pipeline; all null when its shaders write storage (a copy would
			// write it again), their reflection is not available, or one cannot be made.
			std::array<ID3D12PipelineState*, 3> MakeVariants(const Model& model, u64 pipeline) {
				std::array<ID3D12PipelineState*, 3> made = {};
				const ObjectEntry* entry = model.Find(pipeline);
				PipelineCreate p = {};
				if (!entry || !model.DecodeModel(entry->record, entry->recordSize, p)) return made;
				for (const D3D12_SHADER_BYTECODE* code : {&p.vs, &p.ps, &p.gs, &p.hs, &p.ds, &p.as, &p.ms}) {
					bool known = false;
					if (ShaderWritesStorage(*code, known) || !known) return made;
				}
				for (u32 k = kCovered; k < kPassed; ++k) {
					made[k] = device_.Variant(pipeline, [k](PipelineCreate& change) { ProbeChange((Probe)k, change); });
					if (made[k]) continue;
					for (ID3D12PipelineState*& variant : made)
						if (variant) variant->Release(), variant = nullptr;
					return made;
				}
				return made;
			}

			ReplayDevice& device_;
			std::map<u32, DrawProbe>& probes_;
			std::map<u64, std::array<ID3D12PipelineState*, 3>> variants_;
			D3D12_RECT pixel_ = {};
			ID3D12QueryHeap* heap_ = nullptr;
			ID3D12Resource* results_ = nullptr;
			std::vector<u32> used_;
			std::set<u32> ran_;
			u32 open_ = 0;
		};
	}

	void D3D12Replay::PixelHistory(const JsonValue& params, JsonWriter& out) {
		ReplayDevice& device = Device();
		const u32 eid = (u32)params["eid"].Uint(0);
		Spot spot;
		spot.id = params["id"].Uint(0);
		D3D12_RESOURCE_DESC desc;
		spot.subresource = Subresource(spot.id, params, desc);
		if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) Fail("%s is a buffer: Pixel History reads textures", model_.NameOf(spot.id).c_str());
		spot.mip = (u32)params["mip"].Uint(0);
		spot.slice = (u32)params["slice"].Uint(0);
		spot.x = (u32)params["x"].Uint(0);
		spot.y = (u32)params["y"].Uint(0);
		const bool storage = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

		std::vector<HistoryCandidate> candidates;
		std::map<u32, DrawProbe> probes;
		ModelMap map(model_);
		std::vector<u8> args;
		for (const FrameOp& op : model_.frame)
			for (const ListRef& list : op.lists)
				for (const CommandRef& command : list.commands) {
					if (command.eid > eid || !IsAction(command.command)) continue;
					const CmdClass type = ClassOf(command.command);
					HistoryCandidate candidate;
					candidate.eid = command.eid;
					if (type == CmdClass::Draw) {
						const Snapshot s = Simulate(model_, command.eid);
						bool target = Names(model_, s, s.depthStencil, spot);
						for (u64 view : s.renderTargets) target = target || Names(model_, s, view, spot);
						if (!target) continue;
						candidate.draw = command.command != Cmd::ExecuteIndirect;
						if (candidate.draw) {
							candidate.tests.scissor = s.scissors.empty() || Inside(spot.x, spot.y, s.scissors[0].left, s.scissors[0].top, s.scissors[0].right, s.scissors[0].bottom);
							DrawProbe probe;
							probe.slot = (u32)probes.size();
							probe.pipeline = s.pipeline;
							probe.scissors = s.scissors;
							probes[command.eid] = probe;
						}
					}
					else if (type == CmdClass::Dispatch) {
						if (!storage) continue;
					}
					else {
						args.assign(CommandArgsSize(command.command) + 16, 0);
						if (!model_.DecodeCommand(command, args.data(), &map) || !WritesSpot(model_, command, args.data(), spot)) continue;
						candidate.certain = true;
					}
					candidates.push_back(candidate);
				}
		std::sort(candidates.begin(), candidates.end(), [](const HistoryCandidate& a, const HistoryCandidate& b) { return a.eid < b.eid; });
		if (!probes.empty()) {
			ProbeHooks hooks(device, model_, probes, spot);
			device.ReplayTo(eid, &hooks);
			Unreplayed();
			hooks.Results(candidates);
		}

		std::string format;
		HistorySource source;
		source.after = [&](u32 at) { return PixelAt(at, params, format); };
		source.kind = [&](u32 at) {
			const Event* event = model_.EventAt(at);
			return std::string(event ? EventKindName(event->kind) : "?");
		};
		source.name = [&](u32 at) {
			const Event* event = model_.EventAt(at);
			if (!event) return std::string("?");
			const Event* marker = event->parent >= 0 ? &model_.events[(size_t)event->parent] : nullptr;
			return marker ? marker->name + ": " + event->name : event->name;
		};
		out.Uint("id", spot.id);
		out.String("name", model_.NameOf(spot.id));
		out.Uint("x", spot.x);
		out.Uint("y", spot.y);
		out.Uint("mip", spot.mip);
		out.Uint("slice", spot.slice);
		out.Uint("eid", eid);
		WritePixelHistory(candidates, source, eid, out);
		out.String("format", format);
	}
}
