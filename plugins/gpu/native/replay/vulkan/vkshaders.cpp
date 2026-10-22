// A pipeline's shader stage: its SPIR-V disassembled and reflected - bindings by set and binding, blocks with their
// members, the stage's inputs and outputs.
#include "vkreplay.h"
#include "vkspirv.h"

namespace fb::replay::vulkan
{
	static VkShaderStageFlagBits StageOf(const std::string& stage) {
		if (stage == "vs") return VK_SHADER_STAGE_VERTEX_BIT;
		if (stage == "tcs" || stage == "hs") return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (stage == "tes" || stage == "ds") return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (stage == "gs") return VK_SHADER_STAGE_GEOMETRY_BIT;
		if (stage == "cs") return VK_SHADER_STAGE_COMPUTE_BIT;
		if (stage == "task" || stage == "as") return VK_SHADER_STAGE_TASK_BIT_EXT;
		if (stage == "mesh" || stage == "ms") return VK_SHADER_STAGE_MESH_BIT_EXT;
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	void VulkanReplay::Shader(const JsonValue& params, JsonWriter& out) {
		const std::string stage = params["stage"].Str("fs");
		const VkShaderStageFlagBits flag = StageOf(stage);
		u64 pipeline = params["pipeline"].Uint(0);
		if (!pipeline) {
			const Snapshot s = Simulate(model_, (u32)params["eid"].Uint(0));
			pipeline = flag == VK_SHADER_STAGE_COMPUTE_BIT ? s.computePipeline : s.graphicsPipeline;
		}
		const ObjectEntry* entry = model_.Find(pipeline);
		if (!entry) Fail("no pipeline at that event");
		VkPipelineShaderStageCreateInfo found = {};
		bool have = false;
		if (entry->recordType == kComputePipeline) {
			Create<VkComputePipelineCreateInfo> record = {};
			if (model_.DecodeModel(entry->record, entry->recordSize, record) && record.info.stage.stage == flag) {
				found = record.info.stage;
				have = true;
			}
		}
		else {
			Create<VkGraphicsPipelineCreateInfo> record = {};
			if (model_.DecodeModel(entry->record, entry->recordSize, record))
				for (u32 i = 0; i < record.info.stageCount; ++i)
					if (record.info.pStages[i].stage == flag) {
						found = record.info.pStages[i];
						have = true;
					}
		}
		if (!have) Fail("the pipeline has no %s shader", stage.c_str());
		Create<VkShaderModuleCreateInfo> module = {};
		if (!model_.DecodeObject(IdOf(found.module), module) || !module.info.pCode) Fail("the shader module is not in the capture");
		const u32* code = module.info.pCode;
		const size_t words = module.info.codeSize / 4;
		SpirvModule spirv;
		const bool parsed = ParseSpirv(code, words, spirv);
		out.String("stage", stage);
		out.String("pipeline", model_.NameOf(pipeline));
		out.String("kind", "SPIR-V");
		out.Uint("size", module.info.codeSize);
		out.String("model", Format("SPIR-V %u.%u", (spirv.version >> 16) & 0xFF, (spirv.version >> 8) & 0xFF));
		out.String("entry", found.pName ? found.pName : "main");
		out.Uint("instructions", spirv.instructions);
		out.String("disassembly", DisassembleSpirv(code, words));
		if (!parsed) {
			out.String("problem", "the module is not valid SPIR-V");
			return;
		}
		out.BeginArray("bindings");
		for (const SpirvBinding& b : spirv.bindings) {
			if (b.kind == "push constants") continue;
			out.BeginObject();
			out.String("name", b.name);
			out.String("type", b.kind);
			out.String("register", Format("set %u, binding %u", b.set, b.binding));
			out.Uint("space", 0);
			out.Uint("count", b.count);
			out.EndObject();
		}
		out.EndArray();
		out.BeginArray("constantBuffers");
		for (const SpirvBinding& b : spirv.bindings) {
			if (b.members.empty()) continue;
			out.BeginObject();
			out.String("name", b.kind == "push constants" ? "push constants " + b.name : b.name);
			out.Uint("size", b.size);
			out.BeginArray("variables");
			for (const SpirvMember& m : b.members) {
				out.BeginObject();
				out.String("name", m.name);
				out.Uint("offset", m.offset);
				out.Uint("size", m.size);
				out.String("type", m.type);
				out.EndObject();
			}
			out.EndArray();
			out.EndObject();
		}
		out.EndArray();
		auto interface = [&](const char* key, const std::vector<SpirvInterface>& items) {
			out.BeginArray(key);
			for (const SpirvInterface& item : items) {
				out.BeginObject();
				out.String("semantic", item.builtin.empty() ? item.name + " " + item.type : item.builtin);
				out.Uint("register", item.location >= 0 ? (u32)item.location : 0);
				out.Uint("mask", 0);
				out.EndObject();
			}
			out.EndArray();
		};
		interface("inputs", spirv.inputs);
		interface("outputs", spirv.outputs);
	}
}
