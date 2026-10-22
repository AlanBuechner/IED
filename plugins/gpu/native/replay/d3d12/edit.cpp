// Edit and Replay for D3D12 (docs/22 "Edit and Replay"): a stage's shader replaced by HLSL compiled through Windows'
// compilers - DXBC for Shader Model 5.x, DXIL for 6.x - in every pipeline state that used the same bytecode, the
// captured states kept to put back. The replays after draw with the edits; the output diff turns them off and on.
#include "d3d12replay.h"
#include "dxbc.h"
#include "hlsl.h"
#include "shaderedit.h"

#include <set>

namespace fb::replay::d3d12
{
	namespace
	{
		using StageMember = D3D12_SHADER_BYTECODE PipelineCreate::*;

		std::string StageName(const JsonValue& params) {
			std::string stage = params["stage"].Str("ps");
			return stage == "fs" ? "ps" : stage;
		}

		StageMember MemberOf(const std::string& stage) {
			if (stage == "vs") return &PipelineCreate::vs;
			if (stage == "ps") return &PipelineCreate::ps;
			if (stage == "cs") return &PipelineCreate::cs;
			if (stage == "gs") return &PipelineCreate::gs;
			if (stage == "hs") return &PipelineCreate::hs;
			if (stage == "ds") return &PipelineCreate::ds;
			if (stage == "as") return &PipelineCreate::as;
			if (stage == "ms") return &PipelineCreate::ms;
			Fail("no shader stage %s (vs, ps, cs, gs, hs, ds, as, ms)", stage.c_str());
		}

		std::string KeyOf(const D3D12_SHADER_BYTECODE& code) { return ShaderKey(code.pShaderBytecode, code.BytecodeLength); }

		// A shader's profile from its program's version: "ps_5_1", "vs_6_0"; "" when it does not say.
		std::string ProfileOf(const D3D12_SHADER_BYTECODE& code) {
			std::vector<DxbcChunk> chunks;
			if (!ReadDxbc((const u8*)code.pShaderBytecode, code.BytecodeLength, chunks)) return "";
			for (const DxbcChunk& chunk : chunks) {
				if ((chunk.fourcc != FourCC("SHEX") && chunk.fourcc != FourCC("SHDR") && chunk.fourcc != FourCC("DXIL")) || chunk.data.size() < 4) continue;
				u32 version = 0;
				memcpy(&version, chunk.data.data(), 4);
				const char* kinds[] = {"ps", "vs", "gs", "hs", "ds", "cs", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "ms", "as"};
				const u32 kind = version >> 16;
				if (kind >= sizeof(kinds) / sizeof(kinds[0]) || !kinds[kind]) return "";
				return Format("%s_%u_%u", kinds[kind], version >> 4 & 0xF, version & 0xF);
			}
			return "";
		}

		HlslTarget TargetOf(const std::string& profile) {
			const size_t underscore = profile.find('_');
			return underscore != std::string::npos && atoi(profile.c_str() + underscore + 1) >= 6 ? HlslTarget::Dxil : HlslTarget::Dxbc;
		}
	}

	D3D12Replay::~D3D12Replay() {
		if (!device_) return;
		UseEdits(false);
		for (auto& edited : editedPipelines_)
			if (edited.second.edited) edited.second.edited->Release();
		editedPipelines_.clear();
	}

	void D3D12Replay::ShaderSource(const JsonValue& params, JsonWriter& out) {
		const u32 eid = (u32)params["eid"].Uint(0);
		const std::string stage = StageName(params);
		const Snapshot s = Simulate(model_, eid);
		PipelineCreate pso = {};
		const ObjectEntry* entry = model_.Find(s.pipeline);
		if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, pso)) Fail("no pipeline state is bound at event %u", eid);
		const D3D12_SHADER_BYTECODE code = pso.*MemberOf(stage);
		if (!code.pShaderBytecode || !code.BytecodeLength) Fail("the pipeline has no %s stage", stage.c_str());
		const std::string key = KeyOf(code), profile = ProfileOf(code);
		out.String("key", key);
		out.String("stage", stage);
		out.String("pipeline", model_.NameOf(s.pipeline));
		out.String("kind", IsDxil(code) ? "DXIL" : "DXBC");
		out.Uint("size", code.BytecodeLength);
		out.BeginArray("languages").String("hlsl").EndArray();
		for (const ShaderEdit& edit : edits_)
			if (edit.key == key) {
				out.Bool("edited", true).String("language", edit.language).String("profile", edit.profile).String("entry", edit.entry).String("source", edit.source);
				return;
			}
		// A compiled DXBC or DXIL shader keeps no source unless built with debug information: the edit starts from a file.
		out.Bool("edited", false).String("language", "hlsl").String("profile", profile).String("entry", "main");
	}

	std::vector<std::string> D3D12Replay::Rebuild(const std::vector<u64>& pipelines) {
		ReplayDevice& device = Device();
		std::vector<std::string> failed;
		for (u64 id : pipelines) {
			std::vector<const ShaderEdit*> applying;
			for (const ShaderEdit& edit : edits_)
				for (u64 pipeline : edit.pipelines)
					if (pipeline == id) applying.push_back(&edit);
			auto found = editedPipelines_.find(id);
			ID3D12PipelineState* made = nullptr;
			if (!applying.empty()) {
				made = device.Variant(id, [&](PipelineCreate& p) {
					for (const ShaderEdit* edit : applying) p.*MemberOf(edit->stage) = {edit->code.data(), edit->code.size()};
				});
				if (!made) {
					failed.push_back(model_.NameOf(id));
					continue;
				}
			}
			if (found == editedPipelines_.end()) {
				if (made) editedPipelines_[id] = {device.Swap(id, made), made};
				continue;
			}
			EditedPipeline& e = found->second;
			IUnknown* replaced = device.Swap(id, made ? (IUnknown*)made : e.captured);
			if (replaced && replaced != e.captured) replaced->Release();
			if (made) e.edited = made;
			else editedPipelines_.erase(found);
		}
		Unreplayed();
		return failed;
	}

	void D3D12Replay::UseEdits(bool on) {
		if (on == editsOn_ || !device_) return;
		for (auto& edited : editedPipelines_) device_->Swap(edited.first, on ? edited.second.edited : edited.second.captured);
		editsOn_ = on;
		Unreplayed();
	}

	void D3D12Replay::EditShader(const JsonValue& params, JsonWriter& out) {
		Device();
		UseEdits(true);
		const u32 eid = (u32)params["eid"].Uint(0);
		ShaderEdit edit;
		edit.stage = StageName(params);
		const Snapshot s = Simulate(model_, eid);
		PipelineCreate pso = {};
		const ObjectEntry* entry = model_.Find(s.pipeline);
		if (!entry || !model_.DecodeModel(entry->record, entry->recordSize, pso)) Fail("no pipeline state is bound at event %u", eid);
		const StageMember member = MemberOf(edit.stage);
		const D3D12_SHADER_BYTECODE code = pso.*member;
		if (!code.pShaderBytecode || !code.BytecodeLength) Fail("the pipeline has no %s stage", edit.stage.c_str());
		edit.key = KeyOf(code);
		edit.language = params["language"].Str("hlsl");
		if (edit.language != "hlsl") Fail("D3D12 shaders are edited in HLSL");
		edit.profile = params["profile"].Str(ProfileOf(code).c_str());
		edit.entry = params["entry"].Str("main");
		edit.source = params["source"].Str();
		std::string log;
		const bool built = CompileHlsl(edit.source, edit.entry, edit.profile, TargetOf(edit.profile), edit.code, log);
		out.String("key", edit.key).String("stage", edit.stage).String("profile", edit.profile).String("entry", edit.entry);
		if (!built) {
			WriteDiagnostics(out, log.empty() ? "edited.hlsl(1,1): error: it did not compile" : log);
			out.Bool("ok", false);
			return;
		}
		// Every pipeline state with the same shader in this stage.
		for (const auto& object : model_.objects) {
			if (object.second.kind != ObjectKind::PipelineState) continue;
			PipelineCreate p = {};
			if (!model_.DecodeModel(object.second.record, object.second.recordSize, p)) continue;
			const D3D12_SHADER_BYTECODE c = p.*member;
			if (c.pShaderBytecode && KeyOf(c) == edit.key) edit.pipelines.push_back(object.first);
		}
		std::vector<ShaderEdit> before = edits_;
		edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [&](const ShaderEdit& e) { return e.key == edit.key; }), edits_.end());
		edits_.push_back(edit);
		const std::vector<std::string> failed = Rebuild(edit.pipelines);
		if (!failed.empty()) {
			// The edit does not fit a pipeline (its signature, its root signature): put back what was there.
			edits_ = before;
			Rebuild(edit.pipelines);
			log += Format("edited.hlsl(1,1): error: pipeline state %s could not be made with the edit: its inputs, outputs or bindings no longer fit the pipeline\n", failed[0].c_str());
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

	void D3D12Replay::RevertShader(const JsonValue& params, JsonWriter& out) {
		Device();
		UseEdits(true);
		const std::string key = params["key"].Str();
		std::vector<u64> touched;
		u32 reverted = 0;
		for (auto edit = edits_.begin(); edit != edits_.end();) {
			if (!key.empty() && edit->key != key) {
				++edit;
				continue;
			}
			touched.insert(touched.end(), edit->pipelines.begin(), edit->pipelines.end());
			edit = edits_.erase(edit);
			++reverted;
		}
		Rebuild(touched);
		out.Uint("reverted", reverted);
	}

	void D3D12Replay::Edits(JsonWriter& out) {
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
