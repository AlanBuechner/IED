// Pixel History for Vulkan (docs/22 "Pixel History", common/pixelhistory.h): the commands up to an event that may write
// the pixel's subresource - draws and attachment clears with it among the pass's attachments, the passes that begin
// with it attached (their load clears), copies, blits, clears and resolves that name it, dispatches when it allows
// storage writes - then one replay that runs each such draw with its scissor clamped to the pixel inside an occlusion
// query, after copies of its pipeline with every write off that tell coverage, culling and the depth test apart. A
// pipeline with a static scissor is replaced by a clamped copy for the draw itself. The clamped draws leave the rest
// of the frame unwritten, so the replay is forgotten after it.
#include "vkreplay.h"
#include "vkspirv.h"
#include "pixelhistory.h"

#include <algorithm>
#include <array>
#include <set>

namespace fb::replay::vulkan
{
	namespace
	{
		enum Probe : u32 { kCovered, kFacing, kDepth, kPassed, kProbes };

		struct Spot
		{
			u64 id = 0;
			u32 mip = 0;
			u32 layer = 0;
			u32 x = 0;
			u32 y = 0;
		};

		struct DrawProbe
		{
			u32 slot = 0;
			u64 pipeline = 0;
			std::vector<VkRect2D> scissors;    // the draw's own
			bool variants = false;
		};

		// A pipeline's copies for the probes: covered, facing, depth (none when its shaders write storage or its
		// dynamic state would undo the copies' tests) and, when its scissor is static, itself with the scissor clamped.
		struct Copies
		{
			std::array<VkPipeline, 3> probes = {};
			VkPipeline clamped = VK_NULL_HANDLE;
			bool dynamicScissor = false;
			bool usable = false;    // the draw can be measured at all
		};

		// Records every object a command names.
		class RefMap : public ModelMap
		{
		public:
			using ModelMap::ModelMap;
			void* Object(u64 id) override {
				if (id) seen.push_back(id);
				return ModelMap::Object(id);
			}
			std::vector<u64> seen;
		};

		bool InRange(u32 value, u32 first, u32 count) { return value >= first && (count == VK_REMAINING_ARRAY_LAYERS || value < first + count); }

		// Whether view `id` shows the spot's mip and layer of its image.
		bool ViewOfSpot(const Model& model, u64 view, const Spot& spot) {
			Create<VkImageViewCreateInfo> record = {};
			if (!view || !model.DecodeObject(view, record) || IdOf(record.info.image) != spot.id) return false;
			const VkImageSubresourceRange& range = record.info.subresourceRange;
			return InRange(spot.mip, range.baseMipLevel, range.levelCount) && InRange(spot.layer, range.baseArrayLayer, range.layerCount);
		}

		// The attachments of the pass a snapshot is inside (a framebuffer's views, or dynamic rendering's).
		std::vector<u64> Attachments(const Model& model, const Snapshot& s) {
			if (s.rendering) {
				std::vector<u64> views = s.colorViews;
				views.push_back(s.depthView);
				views.push_back(s.stencilView);
				return views;
			}
			if (!s.inPass) return {};
			if (!s.passViews.empty()) return s.passViews;
			Create<VkFramebufferCreateInfo> framebuffer = {};
			std::vector<u64> views;
			if (model.DecodeObject(s.framebuffer, framebuffer) && framebuffer.info.pAttachments)
				for (u32 i = 0; i < framebuffer.info.attachmentCount; ++i) views.push_back(IdOf(framebuffer.info.pAttachments[i]));
			return views;
		}

		bool Attached(const Model& model, const Snapshot& s, const Spot& spot) {
			for (u64 view : Attachments(model, s))
				if (ViewOfSpot(model, view, spot)) return true;
			return false;
		}

		// Whether a command names the spot's image: directly, by a view of it, or by a framebuffer holding one.
		bool Names(const Model& model, const std::vector<u64>& seen, const Spot& spot) {
			for (u64 id : seen) {
				const ObjectEntry* entry = model.Find(id);
				if (id == spot.id || (entry && entry->kind == ObjectKind::ImageView && ViewOfSpot(model, id, spot))) return true;
				Create<VkFramebufferCreateInfo> framebuffer = {};
				if (!entry || entry->kind != ObjectKind::Framebuffer || !model.DecodeObject(id, framebuffer) || !framebuffer.info.pAttachments) continue;
				for (u32 i = 0; i < framebuffer.info.attachmentCount; ++i)
					if (ViewOfSpot(model, IdOf(framebuffer.info.pAttachments[i]), spot)) return true;
			}
			return false;
		}

		// Whether attachments `views` of a pass clear the spot when it begins (`loadOps` theirs, in order).
		bool ClearsSpot(const Model& model, const std::vector<u64>& views, const std::vector<VkAttachmentLoadOp>& loadOps, const Spot& spot) {
			for (size_t i = 0; i < views.size() && i < loadOps.size(); ++i)
				if (loadOps[i] == VK_ATTACHMENT_LOAD_OP_CLEAR && ViewOfSpot(model, views[i], spot)) return true;
			return false;
		}

		// Whether a command that names the spot's image (`raw`, its decoded arguments) writes it for sure: a clear, a
		// copy, blit or resolve into it, a pass that clears it as it begins.
		bool Certain(const Model& model, const CommandRef& command, void* raw, const Spot& spot) {
			switch (command.command) {
				case Cmd::ClearColorImage:
				case Cmd::ClearDepthStencilImage:
				case Cmd::CopyBufferToImage:
				case Cmd::CopyBufferToImage2: return true;
				case Cmd::CopyImage: return IdOf(((CopyImageArgs*)raw)->dst) == spot.id;
				case Cmd::BlitImage: return IdOf(((BlitImageArgs*)raw)->dst) == spot.id;
				case Cmd::ResolveImage: return IdOf(((ResolveImageArgs*)raw)->dst) == spot.id;
				case Cmd::CopyImage2: return ((CopyImage2Args*)raw)->info && IdOf(((CopyImage2Args*)raw)->info->dstImage) == spot.id;
				case Cmd::BlitImage2: return ((BlitImage2Args*)raw)->info && IdOf(((BlitImage2Args*)raw)->info->dstImage) == spot.id;
				case Cmd::ResolveImage2: return ((ResolveImage2Args*)raw)->info && IdOf(((ResolveImage2Args*)raw)->info->dstImage) == spot.id;
				case Cmd::BeginRendering: {
					const VkRenderingInfo* info = ((BeginRenderingArgs*)raw)->info;
					if (!info) return false;
					std::vector<u64> views;
					std::vector<VkAttachmentLoadOp> loads;
					for (u32 i = 0; i < info->colorAttachmentCount; ++i) views.push_back(IdOf(info->pColorAttachments[i].imageView)), loads.push_back(info->pColorAttachments[i].loadOp);
					for (const VkRenderingAttachmentInfo* attachment : {info->pDepthAttachment, info->pStencilAttachment})
						if (attachment) views.push_back(IdOf(attachment->imageView)), loads.push_back(attachment->loadOp);
					return ClearsSpot(model, views, loads, spot);
				}
				case Cmd::BeginRenderPass: {
					const VkRenderPassBeginInfo* begin = ((BeginRenderPassArgs*)raw)->begin;
					Create<VkFramebufferCreateInfo> framebuffer = {};
					if (!begin || !model.DecodeObject(IdOf(begin->framebuffer), framebuffer) || !framebuffer.info.pAttachments) return false;
					std::vector<u64> views;
					for (u32 i = 0; i < framebuffer.info.attachmentCount; ++i) views.push_back(IdOf(framebuffer.info.pAttachments[i]));
					std::vector<VkAttachmentLoadOp> loads;
					RenderPassCreate pass = {};
					if (!model.DecodeObject(IdOf(begin->renderPass), pass)) return false;
					if (pass.version == 2)
						for (u32 i = 0; i < pass.info2.attachmentCount; ++i) loads.push_back(pass.info2.pAttachments[i].loadOp);
					else
						for (u32 i = 0; i < pass.info.attachmentCount; ++i) loads.push_back(pass.info.pAttachments[i].loadOp);
					return ClearsSpot(model, views, loads, spot);
				}
				default: return false;
			}
		}

		bool HasDynamic(const VkGraphicsPipelineCreateInfo& info, std::initializer_list<VkDynamicState> states) {
			if (!info.pDynamicState) return false;
			for (u32 i = 0; i < info.pDynamicState->dynamicStateCount; ++i)
				for (VkDynamicState state : states)
					if (info.pDynamicState->pDynamicStates[i] == state) return true;
			return false;
		}

		// Whether a stage of the pipeline binds storage it could write (a copy of a draw would write it again).
		bool WritesStorage(const Model& model, const VkGraphicsPipelineCreateInfo& info) {
			for (u32 i = 0; i < info.stageCount; ++i) {
				Create<VkShaderModuleCreateInfo> module = {};
				SpirvModule spirv;
				if (!model.DecodeObject(IdOf(info.pStages[i].module), module) || !ParseSpirv(module.info.pCode, module.info.codeSize / 4, spirv)) return true;
				for (const SpirvBinding& binding : spirv.bindings)
					if (binding.kind.find("storage") != std::string::npos) return true;
			}
			return false;
		}

		class ProbeHooks final : public ReplayHooks
		{
		public:
			ProbeHooks(ReplayDevice& device, const Model& model, std::map<u32, DrawProbe>& probes, const Spot& spot) : device_(device), model_(model), probes_(probes) {
				pixel_ = {{(i32)spot.x, (i32)spot.y}, {1, 1}};
				count_ = std::max<u32>(1, (u32)probes.size()) * kProbes;
				VkQueryPoolCreateInfo pool = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
				pool.queryType = VK_QUERY_TYPE_OCCLUSION;
				pool.queryCount = count_;
				if (device.vk.vkCreateQueryPool(device.device, &pool, nullptr, &pool_) != VK_SUCCESS) Fail("the pixel history's occlusion queries could not be made");
				// Reset in a submission of its own, before the replay's.
				VkCommandBuffer cb = device.BeginOwn();
				device.vk.vkCmdResetQueryPool(cb, pool_, 0, count_);
				device.SubmitOwn(cb);
				for (auto& probe : probes) {
					auto made = copies_.find(probe.second.pipeline);
					if (made == copies_.end()) made = copies_.emplace(probe.second.pipeline, MakeCopies(probe.second.pipeline)).first;
					probe.second.variants = made->second.probes[0] != VK_NULL_HANDLE;
				}
			}

			~ProbeHooks() override {
				for (auto& copies : copies_) {
					for (VkPipeline pipeline : copies.second.probes)
						if (pipeline) device_.vk.vkDestroyPipeline(device_.device, pipeline, nullptr);
					if (copies.second.clamped) device_.vk.vkDestroyPipeline(device_.device, copies.second.clamped, nullptr);
				}
				if (pool_) device_.vk.vkDestroyQueryPool(device_.device, pool_, nullptr);
			}

			void BeforeAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				auto found = probes_.find(command.eid);
				if (found == probes_.end()) return;
				const DrawProbe& probe = found->second;
				const Copies& copies = copies_.at(probe.pipeline);
				auto original = (VkPipeline)(uintptr_t)device_.Object(probe.pipeline);
				if (!copies.usable || !original) return;
				const std::vector<VkRect2D> clamped = Clamped(probe.scissors);
				const std::vector<VkRect2D> pixel(clamped.size(), pixel_);
				if (probe.variants) {
					for (u32 k = kCovered; k < kPassed; ++k) {
						device_.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, copies.probes[k]);
						if (copies.dynamicScissor) device_.vk.vkCmdSetScissor(cb, 0, (u32)clamped.size(), k == kCovered ? pixel.data() : clamped.data());
						device_.vk.vkCmdBeginQuery(cb, pool_, probe.slot * kProbes + k, device_.occlusionPrecise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
						device_.RunCommand(cb, command, pass);
						device_.vk.vkCmdEndQuery(cb, pool_, probe.slot * kProbes + k);
					}
				}
				device_.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, copies.dynamicScissor ? original : copies.clamped);
				if (copies.dynamicScissor) device_.vk.vkCmdSetScissor(cb, 0, (u32)clamped.size(), clamped.data());
				device_.vk.vkCmdBeginQuery(cb, pool_, probe.slot * kProbes + kPassed, device_.occlusionPrecise ? VK_QUERY_CONTROL_PRECISE_BIT : 0);
				open_ = command.eid;
			}

			void AfterAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				if (open_ != command.eid) return;
				const DrawProbe& probe = probes_.at(command.eid);
				const Copies& copies = copies_.at(probe.pipeline);
				device_.vk.vkCmdEndQuery(cb, pool_, probe.slot * kProbes + kPassed);
				device_.vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipeline)(uintptr_t)device_.Object(probe.pipeline));
				if (copies.dynamicScissor && !probe.scissors.empty()) device_.vk.vkCmdSetScissor(cb, 0, (u32)probe.scissors.size(), probe.scissors.data());
				ran_.insert(command.eid);
				open_ = 0;
			}

			// The probes' samples, after the replay; a draw the replay did not reach has none.
			void Results(std::vector<HistoryCandidate>& candidates) {
				for (HistoryCandidate& candidate : candidates) {
					auto probe = probes_.find(candidate.eid);
					if (probe == probes_.end() || !ran_.count(candidate.eid)) continue;
					u64 samples[kProbes] = {};
					const u32 first = probe->second.slot * kProbes + (probe->second.variants ? 0 : kPassed);
					const u32 count = probe->second.variants ? kProbes : 1;
					if (device_.vk.vkGetQueryPoolResults(device_.device, pool_, first, count, sizeof(samples), probe->second.variants ? samples : &samples[kPassed], sizeof(u64),
							VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS)
						continue;
					DrawTests& tests = candidate.tests;
					tests.measured = true;
					tests.variants = probe->second.variants;
					tests.covered = samples[kCovered];
					tests.facing = samples[kFacing];
					tests.depth = samples[kDepth];
					tests.passed = samples[kPassed];
				}
			}

		private:
			std::vector<VkRect2D> Clamped(const std::vector<VkRect2D>& scissors) const {
				std::vector<VkRect2D> out;
				for (const VkRect2D& rect : scissors) {
					const i64 left = std::max<i64>(rect.offset.x, pixel_.offset.x), top = std::max<i64>(rect.offset.y, pixel_.offset.y);
					const i64 right = std::min<i64>((i64)rect.offset.x + rect.extent.width, pixel_.offset.x + 1), bottom = std::min<i64>((i64)rect.offset.y + rect.extent.height, pixel_.offset.y + 1);
					out.push_back(left < right && top < bottom ? pixel_ : VkRect2D{{0, 0}, {0, 0}});
				}
				if (out.empty()) out.push_back(pixel_);
				return out;
			}

			// One copy of a pipeline: `writesOff` with the probe's tests, its static scissor set to `scissors`.
			VkPipeline MakeCopy(u64 pipeline, bool writesOff, Probe probe, const std::vector<VkRect2D>& scissors) {
				const ObjectEntry* entry = model_.Find(pipeline);
				Create<VkGraphicsPipelineCreateInfo> record = {};
				if (!entry || !model_.Decode(entry->record, entry->recordSize, record, &device_)) return VK_NULL_HANDLE;
				VkGraphicsPipelineCreateInfo info = record.info;
				info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
				info.basePipelineHandle = VK_NULL_HANDLE;
				info.basePipelineIndex = -1;
				VkPipelineViewportStateCreateInfo viewport = info.pViewportState ? *info.pViewportState : VkPipelineViewportStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
				if (info.pViewportState && viewport.pScissors) {
					viewport.pScissors = scissors.data();
					viewport.scissorCount = (u32)scissors.size();
					info.pViewportState = &viewport;
				}
				VkPipelineRasterizationStateCreateInfo raster = info.pRasterizationState ? *info.pRasterizationState : VkPipelineRasterizationStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
				VkPipelineDepthStencilStateCreateInfo depth = info.pDepthStencilState ? *info.pDepthStencilState : VkPipelineDepthStencilStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
				VkPipelineColorBlendStateCreateInfo blend = info.pColorBlendState ? *info.pColorBlendState : VkPipelineColorBlendStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
				std::vector<VkPipelineColorBlendAttachmentState> attachments;
				if (writesOff) {
					if (blend.pAttachments) attachments.assign(blend.pAttachments, blend.pAttachments + blend.attachmentCount);
					for (VkPipelineColorBlendAttachmentState& attachment : attachments) attachment.colorWriteMask = 0;
					blend.pAttachments = attachments.data();
					depth.depthWriteEnable = VK_FALSE;
					depth.stencilTestEnable = VK_FALSE;
					depth.depthBoundsTestEnable = VK_FALSE;
					if (probe != kDepth) depth.depthTestEnable = VK_FALSE;
					if (probe == kCovered) raster.cullMode = VK_CULL_MODE_NONE;
					if (info.pRasterizationState) info.pRasterizationState = &raster;
					if (info.pDepthStencilState) info.pDepthStencilState = &depth;
					if (info.pColorBlendState) info.pColorBlendState = &blend;
				}
				VkPipeline made = VK_NULL_HANDLE;
				return device_.vk.vkCreateGraphicsPipelines(device_.device, VK_NULL_HANDLE, 1, &info, nullptr, &made) == VK_SUCCESS ? made : VK_NULL_HANDLE;
			}

			Copies MakeCopies(u64 pipeline) {
				Copies copies;
				const ObjectEntry* entry = model_.Find(pipeline);
				Create<VkGraphicsPipelineCreateInfo> record = {};
				if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, record)) return copies;
				const VkGraphicsPipelineCreateInfo& info = record.info;
				copies.dynamicScissor = HasDynamic(info, {VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT});
				const u32 scissorCount = info.pViewportState ? std::max(1u, info.pViewportState->scissorCount) : 1;
				std::vector<VkRect2D> appScissors;
				if (info.pViewportState && info.pViewportState->pScissors) appScissors.assign(info.pViewportState->pScissors, info.pViewportState->pScissors + info.pViewportState->scissorCount);
				const std::vector<VkRect2D> clamped = Clamped(appScissors), pixel(scissorCount, pixel_);
				if (!copies.dynamicScissor) {
					copies.clamped = MakeCopy(pipeline, false, kPassed, clamped);
					if (!copies.clamped) return copies;
				}
				copies.usable = true;
				// Dynamic tests or writes would undo what the copies turn off.
				const bool dynamicTests = HasDynamic(info, {VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
					VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
					VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT, VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT});
				if (dynamicTests || WritesStorage(model_, info)) return copies;
				for (u32 k = kCovered; k < kPassed; ++k) {
					copies.probes[k] = MakeCopy(pipeline, true, (Probe)k, k == kCovered ? pixel : clamped);
					if (copies.probes[k]) continue;
					for (VkPipeline& made : copies.probes)
						if (made) device_.vk.vkDestroyPipeline(device_.device, made, nullptr), made = VK_NULL_HANDLE;
					break;
				}
				return copies;
			}

			ReplayDevice& device_;
			const Model& model_;
			std::map<u32, DrawProbe>& probes_;
			std::map<u64, Copies> copies_;
			VkRect2D pixel_ = {};
			VkQueryPool pool_ = VK_NULL_HANDLE;
			u32 count_ = 0;
			std::set<u32> ran_;
			u32 open_ = 0;
		};
	}

	void VulkanReplay::PixelHistory(const JsonValue& params, JsonWriter& out) {
		ReplayDevice& device = Device();
		const u32 eid = (u32)params["eid"].Uint(0);
		Spot spot;
		spot.id = params["id"].Uint(0);
		ImageCreate image = {};
		const ObjectEntry* entry = model_.Find(spot.id);
		if (!entry || entry->kind != ObjectKind::Image || !model_.DecodeModel(entry->record, entry->recordSize, image)) Fail("%s is not an image: Pixel History reads images", model_.NameOf(spot.id).c_str());
		spot.mip = (u32)params["mip"].Uint(0);
		spot.layer = image.info.imageType == VK_IMAGE_TYPE_3D ? 0 : (u32)params["slice"].Uint(0);
		spot.x = (u32)params["x"].Uint(0);
		spot.y = (u32)params["y"].Uint(0);
		const bool storage = (image.info.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;

		std::vector<HistoryCandidate> candidates;
		std::map<u32, DrawProbe> probes;
		std::vector<u8> args;
		Arena arena;
		for (const FrameOp& op : model_.frame)
			for (const BufferRef& buffer : op.buffers)
				for (const CommandRef& command : buffer.commands) {
					if (command.eid > eid) continue;
					const CmdClass type = ClassOf(command.command);
					const bool begin = command.command == Cmd::BeginRenderPass || command.command == Cmd::BeginRendering;
					if (!IsAction(command.command) && !begin) continue;
					HistoryCandidate candidate;
					candidate.eid = command.eid;
					if (type == CmdClass::Draw) {
						const Snapshot s = Simulate(model_, command.eid);
						if (!Attached(model_, s, spot)) continue;
						candidate.draw = true;
						candidate.tests.scissor = s.scissors.empty() || Inside(spot.x, spot.y, s.scissors[0].offset.x, s.scissors[0].offset.y,
							(i64)s.scissors[0].offset.x + s.scissors[0].extent.width, (i64)s.scissors[0].offset.y + s.scissors[0].extent.height);
						DrawProbe probe;
						probe.slot = (u32)probes.size();
						probe.pipeline = s.graphicsPipeline;
						probe.scissors = s.scissors;
						probes[command.eid] = probe;
					}
					else if (type == CmdClass::Dispatch) {
						if (!storage) continue;
					}
					else if (command.command == Cmd::ClearAttachments) {
						if (!Attached(model_, Simulate(model_, command.eid), spot)) continue;
						candidate.certain = true;
					}
					else {
						RefMap refs(model_);
						args.assign(CommandArgsSize(command.command) + 16, 0);
						arena.Reset();
						if (!model_.DecodeCommandIn(arena, command, args.data(), &refs) || !Names(model_, refs.seen, spot)) continue;
						candidate.certain = Certain(model_, command, args.data(), spot);
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
		source.label = [&](u32 at) {
			const Event* event = model_.EventAt(at);
			if (!event) return std::string("?");
			return event->parent >= 0 ? model_.events[(size_t)event->parent].name : event->name;
		};
		out.Uint("id", spot.id);
		out.String("name", model_.NameOf(spot.id));
		out.Uint("x", spot.x);
		out.Uint("y", spot.y);
		out.Uint("mip", spot.mip);
		out.Uint("slice", (u32)params["slice"].Uint(0));
		out.Uint("eid", eid);
		WritePixelHistory(candidates, source, eid, out);
		out.String("format", format);
	}
}
