// Edit and Replay for Vulkan (docs/22 "Edit and Replay"): a stage's SPIR-V replaced by an edited listing (Flashbang's own
// assembler, so no SPIR-V tools are a dependency) or by HLSL compiled to SPIR-V (a dxcompiler with SPIR-V code
// generation), in every pipeline that used the same module for that stage, the captured pipelines kept to put back.
#include "hlsl.h"
#include "shaderedit.h"
#include "vkreplay.h"
#include "vkspirv.h"

#include <algorithm>

namespace fb::replay::vulkan
{
	namespace
	{
		constexpr u32 kOpSourceContinued = 2, kOpSource = 3;

		std::string StageName(const JsonValue& params) {
			std::string stage = params["stage"].Str("fs");
			return stage == "ps" ? "fs" : stage;
		}

		VkShaderStageFlagBits FlagOf(const std::string& stage) {
			if (stage == "vs") return VK_SHADER_STAGE_VERTEX_BIT;
			if (stage == "fs") return VK_SHADER_STAGE_FRAGMENT_BIT;
			if (stage == "cs") return VK_SHADER_STAGE_COMPUTE_BIT;
			if (stage == "gs") return VK_SHADER_STAGE_GEOMETRY_BIT;
			if (stage == "hs") return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			if (stage == "ds") return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			Fail("no shader stage %s (vs, fs, cs, gs, hs, ds)", stage.c_str());
		}

		// HLSL's profile for a stage, when an edit is written in HLSL.
		std::string ProfileOf(const std::string& stage) { return (stage == "fs" ? std::string("ps") : stage) + "_6_0"; }

		// A pipeline's stages as the model records them: stage -> {module id, entry}.
		std::map<VkShaderStageFlagBits, std::pair<u64, std::string>> StagesOf(const Model& model, u64 pipeline) {
			std::map<VkShaderStageFlagBits, std::pair<u64, std::string>> out;
			const ObjectEntry* entry = model.Find(pipeline);
			if (!entry) return out;
			if (entry->recordType == kComputePipeline) {
				Create<VkComputePipelineCreateInfo> record = {};
				if (model.DecodeObject(pipeline, record)) out[VK_SHADER_STAGE_COMPUTE_BIT] = {IdOf(record.info.stage.module), record.info.stage.pName ? record.info.stage.pName : "main"};
				return out;
			}
			Create<VkGraphicsPipelineCreateInfo> record = {};
			if (!model.DecodeObject(pipeline, record)) return out;
			for (u32 i = 0; i < record.info.stageCount; ++i) {
				const VkPipelineShaderStageCreateInfo& stage = record.info.pStages[i];
				out[stage.stage] = {IdOf(stage.module), stage.pName ? stage.pName : "main"};
			}
			return out;
		}

		std::vector<u32> ModuleWords(const Model& model, u64 module) {
			Create<VkShaderModuleCreateInfo> record = {};
			if (!model.DecodeObject(module, record) || !record.info.pCode) return {};
			return std::vector<u32>(record.info.pCode, record.info.pCode + record.info.codeSize / 4);
		}

		std::string KeyOf(const std::vector<u32>& words) { return ShaderKey(words.data(), words.size() * 4); }

		std::string Literal(const u32* w, size_t count) {
			std::string text;
			for (size_t i = 0; i < count; ++i)
				for (int b = 0; b < 4; ++b) {
					const char c = (char)(w[i] >> (b * 8) & 0xFF);
					if (!c) return text;
					text += c;
				}
			return text;
		}

		// The source a module carries (OpSource with its text, then OpSourceContinued): its language and text.
		bool Embedded(const std::vector<u32>& words, std::string& language, std::string& text) {
			for (size_t at = 5; at < words.size();) {
				const u32 length = words[at] >> 16, op = words[at] & 0xFFFF;
				if (!length || at + length > words.size()) break;
				if (op == kOpSource && length > 4) {
					language = words[at + 1] == 5 ? "hlsl" : words[at + 1] == 2 || words[at + 1] == 1 ? "glsl" : "";
					text = Literal(&words[at + 4], length - 4);
				}
				else if (op == kOpSourceContinued && !text.empty()) text += Literal(&words[at + 1], length - 1);
				at += length;
			}
			return !text.empty();
		}

		bool HasEntry(const std::vector<u32>& words, const std::string& name) {
			SpirvModule module;
			if (!ParseSpirv(words.data(), words.size(), module)) return false;
			for (const SpirvEntry& entry : module.entries)
				if (entry.name == name) return true;
			return false;
		}
	}

	VulkanReplay::~VulkanReplay() {
		if (!device_) return;
		UseEdits(false);
		for (auto& edited : editedPipelines_) device_->vk.vkDestroyPipeline(device_->device, (VkPipeline)(uintptr_t)edited.second.edited, nullptr);
		editedPipelines_.clear();
		for (ShaderEdit& edit : edits_) Forget(edit);
	}

	void VulkanReplay::Forget(ShaderEdit& edit) {
		if (edit.module) device_->vk.vkDestroyShaderModule(device_->device, edit.module, nullptr);
		edit.module = VK_NULL_HANDLE;
	}

	void VulkanReplay::ShaderSource(const JsonValue& params, JsonWriter& out) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const std::string stage = StageName(params);
		const Snapshot s = Simulate(model_, eid);
		const u64 pipeline = stage == "cs" ? s.computePipeline : s.graphicsPipeline;
		const auto stages = StagesOf(model_, pipeline);
		auto found = stages.find(FlagOf(stage));
		if (found == stages.end()) Fail("no %s stage is bound at event %u", stage.c_str(), eid);
		const std::vector<u32> words = ModuleWords(model_, found->second.first);
		if (words.empty()) Fail("the shader module's record is damaged");
		const std::string key = KeyOf(words);
		out.String("key", key).String("stage", stage).String("pipeline", model_.NameOf(pipeline)).String("kind", "SPIR-V").Uint("size", words.size() * 4);
		out.BeginArray("languages").String("spirv");
		if (SpirvDxCompiler()) out.String("hlsl");
		out.EndArray();
		std::string language, text;
		if (Embedded(words, language, text)) out.BeginObject("embedded").String("language", language).String("source", text).EndObject();
		for (const ShaderEdit& edit : edits_)
			if (edit.key == key) {
				out.Bool("edited", true).String("language", edit.language).String("profile", edit.profile).String("entry", edit.entry).String("source", edit.source);
				return;
			}
		out.Bool("edited", false).String("language", "spirv").String("profile", ProfileOf(stage)).String("entry", found->second.second);
		out.String("source", DisassembleSpirv(words.data(), words.size()));
	}

	std::vector<std::string> VulkanReplay::Rebuild(const std::vector<u64>& pipelines) {
		ReplayDevice& device = Device();
		std::vector<std::string> failed;
		for (u64 id : pipelines) {
			std::vector<const ShaderEdit*> applying;
			for (const ShaderEdit& edit : edits_)
				if (std::find(edit.pipelines.begin(), edit.pipelines.end(), id) != edit.pipelines.end()) applying.push_back(&edit);
			auto apply = [&](VkPipelineShaderStageCreateInfo& stage) {
				for (const ShaderEdit* edit : applying)
					if (stage.stage == FlagOf(edit->stage)) {
						stage.module = edit->module;
						stage.pName = edit->entry.c_str();
					}
			};
			VkPipeline made = VK_NULL_HANDLE;
			const ObjectEntry* entry = model_.Find(id);
			if (!applying.empty() && entry) {
				VkResult result = VK_ERROR_UNKNOWN;
				if (entry->recordType == kComputePipeline) {
					Create<VkComputePipelineCreateInfo> record = {};
					if (model_.Decode(entry->record, entry->recordSize, record, &device)) {
						VkComputePipelineCreateInfo info = record.info;
						apply(info.stage);
						info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
						info.basePipelineHandle = VK_NULL_HANDLE;
						info.basePipelineIndex = -1;
						result = device.vk.vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
					}
				}
				else {
					Create<VkGraphicsPipelineCreateInfo> record = {};
					if (model_.Decode(entry->record, entry->recordSize, record, &device)) {
						VkGraphicsPipelineCreateInfo info = record.info;
						std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages, info.pStages + info.stageCount);
						for (VkPipelineShaderStageCreateInfo& stage : stages) apply(stage);
						info.pStages = stages.data();
						info.flags &= ~(VkPipelineCreateFlags)VK_PIPELINE_CREATE_DERIVATIVE_BIT;
						info.basePipelineHandle = VK_NULL_HANDLE;
						info.basePipelineIndex = -1;
						result = device.vk.vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
					}
				}
				if (result != VK_SUCCESS || !made) {
					failed.push_back(model_.NameOf(id));
					continue;
				}
			}
			auto found = editedPipelines_.find(id);
			if (found == editedPipelines_.end()) {
				if (made) editedPipelines_[id] = {device.Swap(id, (u64)(uintptr_t)made), (u64)(uintptr_t)made};
				continue;
			}
			EditedPipeline& e = found->second;
			const u64 replaced = device.Swap(id, made ? (u64)(uintptr_t)made : e.captured);
			if (replaced && replaced != e.captured) device.vk.vkDestroyPipeline(device.device, (VkPipeline)(uintptr_t)replaced, nullptr);
			if (made) e.edited = (u64)(uintptr_t)made;
			else editedPipelines_.erase(found);
		}
		Unreplayed();
		return failed;
	}

	void VulkanReplay::UseEdits(bool on) {
		if (on == editsOn_ || !device_) return;
		for (auto& edited : editedPipelines_) device_->Swap(edited.first, on ? edited.second.edited : edited.second.captured);
		editsOn_ = on;
		Unreplayed();
	}

	void VulkanReplay::EditShader(const JsonValue& params, JsonWriter& out) {
		ReplayDevice& device = Device();
		UseEdits(true);
		const u32 eid = (u32)params["eid"].Uint(0);
		ShaderEdit edit;
		edit.stage = StageName(params);
		const VkShaderStageFlagBits flag = FlagOf(edit.stage);
		const Snapshot s = Simulate(model_, eid);
		const auto stages = StagesOf(model_, edit.stage == "cs" ? s.computePipeline : s.graphicsPipeline);
		auto found = stages.find(flag);
		if (found == stages.end()) Fail("no %s stage is bound at event %u", edit.stage.c_str(), eid);
		const std::vector<u32> original = ModuleWords(model_, found->second.first);
		if (original.empty()) Fail("the shader module's record is damaged");
		edit.key = KeyOf(original);
		edit.language = params["language"].Str("spirv");
		edit.source = params["source"].Str();
		edit.profile = params["profile"].Str(ProfileOf(edit.stage).c_str());
		edit.entry = edit.language == "spirv" ? found->second.second : params["entry"].Str("main");
		std::string log;
		bool built = false;
		if (edit.language == "spirv") built = AssembleSpirv(edit.source, original.data(), original.size(), edit.words, log);
		else if (edit.language == "hlsl") {
			std::vector<u8> code;
			built = CompileHlsl(edit.source, edit.entry, edit.profile, HlslTarget::Spirv, code, log) && code.size() % 4 == 0;
			edit.words.assign((const u32*)code.data(), (const u32*)code.data() + code.size() / 4);
		}
		else Fail("Vulkan shaders are edited as SPIR-V assembly (spirv) or HLSL");
		// A module without the entry the pipeline names would not make one: say so rather than hand it to the driver.
		if (built && !HasEntry(edit.words, edit.entry)) {
			built = false;
			log += Format("line 1: the module has no entry point \"%s\"\n", edit.entry.c_str());
		}
		out.String("key", edit.key).String("stage", edit.stage).String("profile", edit.profile).String("entry", edit.entry);
		if (!built) {
			WriteDiagnostics(out, log);
			out.Bool("ok", false);
			return;
		}
		VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = edit.words.size() * 4;
		info.pCode = edit.words.data();
		if (device.vk.vkCreateShaderModule(device.device, &info, nullptr, &edit.module) != VK_SUCCESS) Fail("the edited shader module could not be made");
		for (const auto& object : model_.objects) {
			if (object.second.kind != ObjectKind::Pipeline) continue;
			const auto its = StagesOf(model_, object.first);
			auto stage = its.find(flag);
			if (stage != its.end() && KeyOf(ModuleWords(model_, stage->second.first)) == edit.key) edit.pipelines.push_back(object.first);
		}
		std::vector<ShaderEdit> before;
		for (ShaderEdit& e : edits_) {
			if (e.key == edit.key) Forget(e);
			else before.push_back(e);
		}
		edits_ = before;
		edits_.push_back(edit);
		const std::vector<std::string> failed = Rebuild(edit.pipelines);
		if (!failed.empty()) {
			Forget(edits_.back());
			edits_.pop_back();
			Rebuild(edit.pipelines);
			log += Format("line 1: pipeline %s could not be made with the edit: its interface or bindings no longer fit the pipeline\n", failed[0].c_str());
			WriteDiagnostics(out, log);
			out.Bool("ok", false);
			return;
		}
		WriteDiagnostics(out, log);
		out.Bool("ok", true);
		out.BeginArray("pipelines");
		for (u64 id : edit.pipelines) out.String(model_.NameOf(id));
		out.EndArray();
	}

	void VulkanReplay::RevertShader(const JsonValue& params, JsonWriter& out) {
		Device();
		UseEdits(true);
		const std::string key = params["key"].Str();
		std::vector<u64> touched;
		std::vector<ShaderEdit> kept, gone;
		for (ShaderEdit& edit : edits_) (key.empty() || edit.key == key ? gone : kept).push_back(edit);
		for (const ShaderEdit& edit : gone) touched.insert(touched.end(), edit.pipelines.begin(), edit.pipelines.end());
		edits_ = kept;
		Rebuild(touched);
		for (ShaderEdit& edit : gone) Forget(edit);
		out.Uint("reverted", gone.size());
	}

	void VulkanReplay::Edits(JsonWriter& out) {
		out.BeginArray("edits");
		for (const ShaderEdit& edit : edits_) {
			out.BeginObject().String("key", edit.key).String("stage", edit.stage).String("language", edit.language).String("entry", edit.entry).String("profile", edit.profile);
			out.BeginArray("pipelines");
			for (u64 id : edit.pipelines) out.String(model_.NameOf(id));
			out.EndArray().EndObject();
		}
		out.EndArray();
	}
}
