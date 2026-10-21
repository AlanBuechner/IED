#include "session.h"

#include <cstdio>

namespace fb::replay
{
	void Fail(const char* format, ...) {
		va_list args;
		va_start(args, format);
		Failure failure = {FormatV(format, args)};
		va_end(args);
		throw failure;
	}

	void Session::Initialize(const JsonValue& params, JsonWriter& out) {
		const std::string path = params["capture"].Str();
		if (path.empty()) Fail("initialize needs the capture's path");
		file_ = std::make_unique<CaptureReader>();
		std::string error;
		if (!file_->Open(Widen(path), error)) Fail("%s", error.c_str());
		switch (file_->GetApi()) {
			case Api::D3D12: replay_ = MakeD3D12Replay(); break;
			case Api::Vulkan: replay_ = MakeVulkanReplay(); break;
			default: Fail("the capture's API is not known");
		}
		out.String("capture", path);
		out.Uint("bytes", file_->FileSize());
		out.Uint("formatVersion", file_->Version());
		replay_->Open(*file_, params["options"], out);
	}

	Answer Session::Handle(const std::string& method, const JsonValue& params, const Progress& progress) {
		Answer answer;
		JsonWriter out;
		out.BeginObject();
		try {
			if (method == "initialize") Initialize(params, out);
			else if (!replay_) Fail("no capture is open: send initialize first");
			else if (method == "events") replay_->Events(out);
			else if (method == "apiCalls") replay_->ApiCalls((u32)params["eid"].Uint(0), out);
			else if (method == "state") replay_->State((u32)params["eid"].Uint(0), out);
			else if (method == "resources") replay_->Resources(out);
			else if (method == "texture") replay_->Texture(params, out, answer.binary);
			else if (method == "pixel") replay_->Pixel(params, out);
			else if (method == "buffer") replay_->Buffer(params, out, answer.binary);
			else if (method == "mesh") replay_->Mesh(params, out, answer.binary);
			else if (method == "shader") replay_->Shader(params, out);
			else if (method == "timing") replay_->Timing(params, out, progress);
			else if (method == "messages") replay_->Messages(out);
			else Fail("unknown method %s", method.c_str());
		}
		catch (const Failure& failure) {
			answer.error = failure.message.empty() ? std::string("failed") : failure.message;
			answer.binary.clear();
			return answer;
		}
		catch (const std::exception& exception) {
			answer.error = exception.what();
			answer.binary.clear();
			return answer;
		}
		out.EndObject();
		answer.result = out.Take();
		return answer;
	}
}
