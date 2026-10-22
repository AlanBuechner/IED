// d3d12replay.h - the D3D12 replay behind the ied-gpu/1 requests (replay.h): the model of the capture, the replay
// device, and what each request needs between calls (the last subresource read, for pixel picks at the same place).
#pragma once

#include "device.h"
#include "simulate.h"
#include "texels.h"

namespace fb::replay::d3d12
{
	struct Readback
	{
		u32 eid = 0;
		u64 id = 0;
		u32 subresource = 0;
		D3D12_SUBRESOURCE_FOOTPRINT footprint = {};
		u32 rows = 0;
		std::vector<u8> bytes;
		bool valid = false;
	};

	struct Usage
	{
		u32 first = 0;
		u32 last = 0;
		u32 count = 0;
	};

	// Whether a shader is DXIL (Shader Model 6) rather than DXBC.
	bool IsDxil(const D3D12_SHADER_BYTECODE& code);
	// A DXBC shader's listing for the shader debugger, and each instruction's line in it by its byte offset in the
	// program (common/dxbc.h); empty when d3dcompiler_47.dll cannot disassemble it.
	std::string DxbcListing(const D3D12_SHADER_BYTECODE& code, std::map<u32, u32>& lines);
	// A buffer in a readback heap of at least `bytes`, in the copy destination state; the caller releases it.
	ID3D12Resource* MakeReadback(ID3D12Device* device, u64 bytes);
	// Whether a shader binds anything it can write (a UAV); `known` false when its reflection is not available.
	bool ShaderWritesStorage(const D3D12_SHADER_BYTECODE& code, bool& known);
	// Each constant buffer the pipeline's shaders declare, by (space, register): the layout the buffer view reads it with.
	std::map<std::pair<u32, u32>, std::string> ConstantBufferLayouts(const PipelineCreate& pso);

	class D3D12Replay final : public ApiReplay
	{
	public:
		void Open(CaptureReader& file, const JsonValue& options, JsonWriter& info) override;
		void Events(JsonWriter& out) override;
		void ApiCalls(u32 eid, JsonWriter& out) override;
		void State(u32 eid, JsonWriter& out) override;
		void Resources(JsonWriter& out) override;
		void Texture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Pixel(const JsonValue& params, JsonWriter& out) override;
		void PixelHistory(const JsonValue& params, JsonWriter& out) override;
		void DebugShader(const JsonValue& params, JsonWriter& out) override;
		void Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Shader(const JsonValue& params, JsonWriter& out) override;
		void Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) override;
		void Messages(JsonWriter& out) override;
		void ShaderSource(const JsonValue& params, JsonWriter& out) override;
		void EditShader(const JsonValue& params, JsonWriter& out) override;
		void RevertShader(const JsonValue& params, JsonWriter& out) override;
		void Edits(JsonWriter& out) override;
		void UseEdits(bool on) override;
		~D3D12Replay() override;

		// The device, made at Open unless the options asked for none (a model-only replay, for tests and the CLI).
		ReplayDevice& Device();
		const Model& GetModel() const { return model_; }

	private:
		void ComputeUsage();
		const Readback& Read(u32 eid, u64 id, u32 subresource);
		void ReadBuffer(u32 eid, u64 id, u64 offset, u64 size, std::vector<u8>& out);
		u32 Subresource(u64 id, const JsonValue& params, D3D12_RESOURCE_DESC& desc);
		// The texel the pixel request `params` names, after event `eid` (0: at the frame's start); `format` gets its name.
		TexelValue PixelAt(u32 eid, const JsonValue& params, std::string& format);
		// Forgets what the last replay left, after one that changed the frame (the pixel history's clamped draws).
		void Unreplayed() {
			replayedTo_ = UINT32_MAX;
			last_ = Readback();
		}

		// Edit and Replay (edit.cpp): each edited shader, by its key, and each pipeline state built with edits. While the
		// edits are on the device holds the edited states and these keep the captured ones; off, the other way round.
		struct ShaderEdit
		{
			std::string key, stage, language, entry, profile, source;
			std::vector<u8> code;
			std::vector<u64> pipelines;
		};
		struct EditedPipeline
		{
			IUnknown* captured = nullptr;
			IUnknown* edited = nullptr;
		};
		// Makes `pipelines` again with every edit that applies to them (with none, the captured state comes back); the
		// names of those that could not be made.
		std::vector<std::string> Rebuild(const std::vector<u64>& pipelines);

		Model model_;
		std::unique_ptr<ReplayDevice> device_;
		std::vector<ShaderEdit> edits_;
		std::map<u64, EditedPipeline> editedPipelines_;
		bool editsOn_ = true;
		std::map<u64, Usage> usage_;
		Readback last_;
		u32 replayedTo_ = UINT32_MAX;
		std::string deviceProblem_;
	};
}
