// d3d12replay.h - the D3D12 replay behind the ied-gpu/1 requests (replay.h): the model of the capture, the replay
// device, and what each request needs between calls (the last subresource read, for pixel picks at the same place).
#pragma once

#include "device.h"
#include "simulate.h"

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
		void Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) override;
		void Shader(const JsonValue& params, JsonWriter& out) override;
		void Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) override;
		void Messages(JsonWriter& out) override;

		// The device, made at Open unless the options asked for none (a model-only replay, for tests and the CLI).
		ReplayDevice& Device();
		const Model& GetModel() const { return model_; }

	private:
		void ComputeUsage();
		const Readback& Read(u32 eid, u64 id, u32 subresource);
		void ReadBuffer(u32 eid, u64 id, u64 offset, u64 size, std::vector<u8>& out);
		u32 Subresource(u64 id, const JsonValue& params, D3D12_RESOURCE_DESC& desc);

		Model model_;
		std::unique_ptr<ReplayDevice> device_;
		std::map<u64, Usage> usage_;
		Readback last_;
		u32 replayedTo_ = UINT32_MAX;
		std::string deviceProblem_;
	};
}
