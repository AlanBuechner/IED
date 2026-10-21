// replay.h - what the replayer asks of an API's replay (docs/22 "Replay"): each method answers one ied-gpu/1 request
// with JSON (and, for data, a binary part). The D3D12 and Vulkan replays implement it; session.cpp dispatches to them.
#pragma once

#include "capfile.h"
#include "json.h"

#include <functional>

namespace fb::replay
{
	// A request's failure: a message for the UI.
	struct Failure
	{
		std::string message;
	};

	using Progress = std::function<void(const char* what, double fraction)>;

	class ApiReplay
	{
	public:
		virtual ~ApiReplay() = default;

		// Reads the capture and makes the replay device; `info` gets the summary (initialize's result).
		virtual void Open(CaptureReader& file, const JsonValue& options, JsonWriter& info) = 0;
		virtual void Events(JsonWriter& out) = 0;
		virtual void ApiCalls(u32 eid, JsonWriter& out) = 0;
		virtual void State(u32 eid, JsonWriter& out) = 0;
		virtual void Resources(JsonWriter& out) = 0;
		virtual void Texture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) = 0;
		virtual void Pixel(const JsonValue& params, JsonWriter& out) = 0;
		virtual void Buffer(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) = 0;
		virtual void Mesh(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) = 0;
		virtual void Shader(const JsonValue& params, JsonWriter& out) = 0;
		virtual void Timing(const JsonValue& params, JsonWriter& out, const Progress& progress) = 0;
		virtual void Messages(JsonWriter& out) = 0;
	};

	std::unique_ptr<ApiReplay> MakeD3D12Replay();
	std::unique_ptr<ApiReplay> MakeVulkanReplay();

	[[noreturn]] void Fail(const char* format, ...);
}
