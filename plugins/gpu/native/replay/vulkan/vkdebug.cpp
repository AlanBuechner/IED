// Shader debugging for Vulkan (docs/22 "Shader debugging"): the event's shader stage instrumented for the chosen pixel,
// vertex or compute thread (vkinstrument.h), run once in the action's place with the trace buffer bound in a descriptor
// set after the pipeline's own, and the trace read back - every value each chosen invocation made, in order, against
// the stage's listing. Stepping back is reading the trace backwards.
#include "vkinstrument.h"
#include "vkreplay.h"
#include "vkspirv.h"

#include <cmath>
#include <cstring>

namespace fb::replay::vulkan
{
	namespace
	{
		// What a debug run makes, destroyed with it.
		struct DebugObjects
		{
			explicit DebugObjects(ReplayDevice& device) : d(device) {}
			~DebugObjects() {
				if (pipeline) d.vk.vkDestroyPipeline(d.device, pipeline, nullptr);
				if (layout) d.vk.vkDestroyPipelineLayout(d.device, layout, nullptr);
				if (pool) d.vk.vkDestroyDescriptorPool(d.device, pool, nullptr);
				if (setLayout) d.vk.vkDestroyDescriptorSetLayout(d.device, setLayout, nullptr);
				if (module) d.vk.vkDestroyShaderModule(d.device, module, nullptr);
				if (buffer) d.vk.vkDestroyBuffer(d.device, buffer, nullptr);
				if (memory) d.vk.vkFreeMemory(d.device, memory, nullptr);
			}

			ReplayDevice& d;
			VkShaderModule module = VK_NULL_HANDLE;
			VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
			VkPipelineLayout layout = VK_NULL_HANDLE;
			VkPipeline pipeline = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			VkDescriptorSet set = VK_NULL_HANDLE;
			VkBuffer buffer = VK_NULL_HANDLE;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			u32* mapped = nullptr;
		};

		// Runs the instrumented pipeline in the action's place; the action itself runs after, as it was.
		class DebugHooks final : public ReplayHooks
		{
		public:
			DebugHooks(ReplayDevice& device, const DebugObjects& objects, u32 eid, VkPipelineBindPoint point, u64 pipeline, u32 set)
				: device_(device), objects_(objects), eid_(eid), point_(point), pipeline_(pipeline), set_(set) {}

			void BeforeAction(VkCommandBuffer cb, const CommandRef& command, PassState& pass) override {
				if (command.eid != eid_) return;
				device_.vk.vkCmdBindPipeline(cb, point_, objects_.pipeline);
				device_.vk.vkCmdBindDescriptorSets(cb, point_, objects_.layout, set_, 1, &objects_.set, 0, nullptr);
				device_.RunCommand(cb, command, pass);
				device_.vk.vkCmdBindPipeline(cb, point_, (VkPipeline)(uintptr_t)device_.Object(pipeline_));
				ran = true;
			}

			bool ran = false;

		private:
			ReplayDevice& device_;
			const DebugObjects& objects_;
			u32 eid_;
			VkPipelineBindPoint point_;
			u64 pipeline_;
			u32 set_;
		};

		std::string ValueText(const TraceSite& site, const u32* words) {
			std::string text;
			for (u32 k = 0; k < site.components; ++k) {
				if (k) text += ", ";
				const u32 w = words[k];
				switch (site.type) {
					case 0: {
						float f;
						memcpy(&f, &w, 4);
						text += std::isnan(f) ? "NaN" : std::isinf(f) ? (f > 0 ? "Inf" : "-Inf") : Format("%.6g", f);
						break;
					}
					case 1: text += Format("%d", (i32)w); break;
					case 2: text += Format("%u", w); break;
					default: text += w ? "true" : "false"; break;
				}
			}
			return text;
		}

		// The stage of a pipeline the request names, its module and entry point, and its pipeline layout.
		struct Stage
		{
			u64 module = 0;
			std::string entry;
			u64 layout = 0;
			u32 model = 0;
			VkShaderStageFlagBits flag = VK_SHADER_STAGE_FRAGMENT_BIT;
		};

		Stage FindStage(const Model& model, u64 pipeline, const std::string& stage, bool compute) {
			Stage found;
			if (compute) {
				Create<VkComputePipelineCreateInfo> record = {};
				if (!model.DecodeObject(pipeline, record)) Fail("the compute pipeline's record is damaged");
				found.module = IdOf(record.info.stage.module);
				found.entry = record.info.stage.pName ? record.info.stage.pName : "main";
				found.layout = IdOf(record.info.layout);
				found.model = 5;
				found.flag = VK_SHADER_STAGE_COMPUTE_BIT;
				return found;
			}
			found.flag = stage == "vs" ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
			found.model = stage == "vs" ? 0 : 4;
			Create<VkGraphicsPipelineCreateInfo> record = {};
			if (!model.DecodeObject(pipeline, record)) Fail("the graphics pipeline's record is damaged");
			found.layout = IdOf(record.info.layout);
			for (u32 i = 0; i < record.info.stageCount; ++i)
				if (record.info.pStages[i].stage == found.flag) {
					found.module = IdOf(record.info.pStages[i].module);
					found.entry = record.info.pStages[i].pName ? record.info.pStages[i].pName : "main";
				}
			if (!found.module) Fail("the pipeline has no %s stage", stage.c_str());
			return found;
		}
	}

	void VulkanReplay::DebugShader(const JsonValue& params, JsonWriter& out) {
		ReplayDevice& device = Device();
		const u32 eid = (u32)params["eid"].Uint(0);
		std::string stage = params["stage"].Str("fs");
		if (stage == "ps") stage = "fs";
		if (stage != "vs" && stage != "fs" && stage != "cs") Fail("the shader debugger steps vertex (vs), fragment (fs) and compute (cs) shaders");
		const bool compute = stage == "cs";
		const Snapshot s = Simulate(model_, eid);
		if (!s.event) Fail("there is no event %u", eid);
		const CmdClass action = ClassOf(s.command);
		if (compute ? action != CmdClass::Dispatch : action != CmdClass::Draw) Fail("event %u is not a %s", eid, compute ? "dispatch" : "draw");
		if (!compute && !device.storesInShaders) Fail("this GPU's vertex and fragment shaders cannot write storage buffers, which the shader debugger's trace needs");
		const u64 pipeline = compute ? s.computePipeline : s.graphicsPipeline;
		const Stage found = FindStage(model_, pipeline, stage, compute);
		Create<VkShaderModuleCreateInfo> module = {};
		if (!model_.DecodeObject(found.module, module) || !module.info.pCode) Fail("the shader module's record is damaged");
		const u32* code = module.info.pCode;
		const size_t words = module.info.codeSize / 4;
		Create<VkPipelineLayoutCreateInfo> layout = {};
		if (!model_.DecodeObject(found.layout, layout)) Fail("the pipeline layout's record is damaged");

		TraceTarget target;
		target.model = found.model;
		target.set = layout.info.setLayoutCount;
		target.x = (u32)params["x"].Uint(0);
		target.y = (u32)params["y"].Uint(0);
		target.vertex = (u32)params["vertex"].Uint(0);
		target.instance = (u32)params["instance"].Uint(0);
		for (u32 k = 0; k < 3; ++k) target.thread[k] = (u32)params["thread"].At(k).Uint(0);
		if (target.set >= device.properties.limits.maxBoundDescriptorSets) Fail("the pipeline uses every descriptor set this GPU binds; the trace needs one more");
		Instrumented instrumented;
		if (!InstrumentSpirv(code, words, found.entry, target, instrumented)) Fail("the shader cannot be instrumented: %s", instrumented.problem.c_str());

		DebugObjects objects(device);
		VkShaderModuleCreateInfo moduleInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = instrumented.words.size() * 4;
		moduleInfo.pCode = instrumented.words.data();
		if (device.vk.vkCreateShaderModule(device.device, &moduleInfo, nullptr, &objects.module) != VK_SUCCESS) Fail("the instrumented shader module could not be made");
		VkDescriptorSetLayoutBinding binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr};
		VkDescriptorSetLayoutCreateInfo setInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		setInfo.bindingCount = 1;
		setInfo.pBindings = &binding;
		if (device.vk.vkCreateDescriptorSetLayout(device.device, &setInfo, nullptr, &objects.setLayout) != VK_SUCCESS) Fail("the trace's descriptor set layout could not be made");
		std::vector<VkDescriptorSetLayout> sets;
		for (u32 i = 0; i < layout.info.setLayoutCount; ++i) sets.push_back((VkDescriptorSetLayout)(uintptr_t)device.Object(IdOf(layout.info.pSetLayouts[i])));
		sets.push_back(objects.setLayout);
		VkPipelineLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layoutInfo.setLayoutCount = (u32)sets.size();
		layoutInfo.pSetLayouts = sets.data();
		layoutInfo.pushConstantRangeCount = layout.info.pushConstantRangeCount;
		layoutInfo.pPushConstantRanges = layout.info.pPushConstantRanges;
		if (device.vk.vkCreatePipelineLayout(device.device, &layoutInfo, nullptr, &objects.layout) != VK_SUCCESS) Fail("the instrumented pipeline layout could not be made");

		const ObjectEntry* entry = model_.Find(pipeline);
		VkResult made = VK_ERROR_UNKNOWN;
		if (compute) {
			Create<VkComputePipelineCreateInfo> record = {};
			if (!entry || !model_.Decode(entry->record, entry->recordSize, record, &device)) Fail("the compute pipeline's record is damaged");
			VkComputePipelineCreateInfo info = record.info;
			info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
			info.basePipelineHandle = VK_NULL_HANDLE;
			info.basePipelineIndex = -1;
			info.stage.module = objects.module;
			info.layout = objects.layout;
			made = device.vk.vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &objects.pipeline);
		}
		else {
			Create<VkGraphicsPipelineCreateInfo> record = {};
			if (!entry || !model_.Decode(entry->record, entry->recordSize, record, &device)) Fail("the graphics pipeline's record is damaged");
			VkGraphicsPipelineCreateInfo info = record.info;
			std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages, info.pStages + info.stageCount);
			for (VkPipelineShaderStageCreateInfo& one : stages)
				if (one.stage == found.flag) one.module = objects.module;
			info.pStages = stages.data();
			info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
			info.basePipelineHandle = VK_NULL_HANDLE;
			info.basePipelineIndex = -1;
			info.layout = objects.layout;
			made = device.vk.vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &objects.pipeline);
		}
		if (made != VK_SUCCESS) Fail("the instrumented pipeline could not be made (VkResult %d)", (int)made);

		VkBufferCreateInfo bufferInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferInfo.size = (u64)kTraceWords * 4;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (device.vk.vkCreateBuffer(device.device, &bufferInfo, nullptr, &objects.buffer) != VK_SUCCESS) Fail("the trace buffer could not be made");
		VkMemoryRequirements needs = {};
		device.vk.vkGetBufferMemoryRequirements(device.device, objects.buffer, &needs);
		VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocation.allocationSize = needs.size;
		allocation.memoryTypeIndex = device.MemoryType(needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (allocation.memoryTypeIndex == UINT32_MAX || device.vk.vkAllocateMemory(device.device, &allocation, nullptr, &objects.memory) != VK_SUCCESS ||
			device.vk.vkBindBufferMemory(device.device, objects.buffer, objects.memory, 0) != VK_SUCCESS ||
			device.vk.vkMapMemory(device.device, objects.memory, 0, VK_WHOLE_SIZE, 0, (void**)&objects.mapped) != VK_SUCCESS)
			Fail("the trace buffer's memory could not be made");
		memset(objects.mapped, 0, (size_t)kTraceWords * 4);
		objects.mapped[0] = kTraceFirst;
		VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
		VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		VkDescriptorSetAllocateInfo setAllocation = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		setAllocation.descriptorSetCount = 1;
		setAllocation.pSetLayouts = &objects.setLayout;
		if (device.vk.vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &objects.pool) != VK_SUCCESS ||
			(setAllocation.descriptorPool = objects.pool, device.vk.vkAllocateDescriptorSets(device.device, &setAllocation, &objects.set)) != VK_SUCCESS)
			Fail("the trace's descriptor set could not be made");
		VkDescriptorBufferInfo described = {objects.buffer, 0, VK_WHOLE_SIZE};
		VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = objects.set;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write.pBufferInfo = &described;
		device.vk.vkUpdateDescriptorSets(device.device, 1, &write, 0, nullptr);

		DebugHooks hooks(device, objects, eid, compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, target.set);
		device.ReplayTo(eid, &hooks);
		Unreplayed();
		if (!hooks.ran) Fail("the replay did not reach event %u", eid);

		std::map<size_t, u32> lines;
		const std::string listing = DisassembleSpirv(code, words, &lines);
		out.Uint("eid", eid);
		out.String("stage", stage);
		out.String("pipeline", model_.NameOf(pipeline));
		out.String("entry", found.entry);
		out.String("listing", listing);
		out.BeginArray("sites");
		for (const TraceSite& site : instrumented.sites) {
			auto line = lines.find(site.offset);
			out.BeginArray().Uint(line != lines.end() ? line->second : 0).Uint(site.type).Uint(site.components).Bool(site.store).EndArray();
		}
		out.EndArray();
		const u32* trace = objects.mapped;
		const u32 used = std::min(trace[0], kTraceWords);
		const u32 region = kTraceRecords * kTraceRecordWords;
		const u32 regions = used > kTraceFirst ? std::min((used - kTraceFirst) / region, kTraceRegions) : 0;
		out.BeginArray("invocations");
		for (u32 r = 0; r < regions; ++r) {
			const u32* records = trace + kTraceFirst + (size_t)r * region;
			out.BeginObject();
			out.BeginArray("steps");
			u32 k = 0;
			for (; k < kTraceRecords; ++k) {
				const u32* record = records + (size_t)k * kTraceRecordWords;
				if (!record[0] || record[0] > instrumented.sites.size()) break;
				out.BeginArray().Uint(record[0] - 1).String(ValueText(instrumented.sites[record[0] - 1], record + 1)).EndArray();
			}
			out.EndArray();
			out.Bool("truncated", k == kTraceRecords);
			out.EndObject();
		}
		out.EndArray();
	}
}
