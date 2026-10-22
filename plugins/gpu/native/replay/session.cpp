#include "session.h"
#include "texels.h"

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

	std::unique_ptr<ApiReplay> MakeReplay(Api api) {
		switch (api) {
			case Api::D3D12: return MakeD3D12Replay();
			case Api::Vulkan: return MakeVulkanReplay();
			default: Fail("the capture's API is not known");
		}
	}

	void Session::Initialize(const JsonValue& params, JsonWriter& out) {
		const std::string path = params["capture"].Str();
		if (path.empty()) Fail("initialize needs the capture's path");
		other_.reset();
		otherFile_.reset();
		file_ = std::make_unique<CaptureReader>();
		std::string error;
		if (!file_->Open(Widen(path), error)) Fail("%s", error.c_str());
		replay_ = MakeReplay(file_->GetApi());
		out.String("capture", path);
		out.Uint("bytes", file_->FileSize());
		out.Uint("formatVersion", file_->Version());
		replay_->Open(*file_, params["options"], out);
	}

	// A texture at an event as a PNG file a model can look at (`texture`'s params with `path`): the texture's answer,
	// the path, and the range the 8 bits span - `range` when given, else 0..1, or up to the brightest color of a float
	// texture brighter than 1.
	void Session::Png(const JsonValue& params, JsonWriter& out) {
		const std::string path = params["path"].Str();
		if (path.empty()) Fail("png needs the path to write");
		JsonWriter texture;
		std::vector<u8> rgba;
		texture.BeginObject();
		Side(params).Texture(params, texture, rgba);
		texture.EndObject();
		JsonValue shown;
		std::string error;
		if (!JsonValue::Parse(texture.Text().data(), texture.Text().size(), shown, error)) Fail("the texture's answer does not parse: %s", error.c_str());
		double lo = 0, hi = 1;
		const JsonValue& range = params["range"];
		if (range.Count() == 2) lo = range.At(0).Number(), hi = range.At(1).Number(1);
		else if (shown["linear"].Bool())
			for (size_t c = 0; c < 3; ++c) hi = std::max(hi, shown["max"].At(c).Number());
		if (!SaveShownPng(shown, rgba, lo, hi, Widen(path))) Fail("the PNG could not be written to %s", path.c_str());
		out.Raw("texture", texture.Text());
		out.String("path", path);
		out.BeginArray("range").Double(lo).Double(hi).EndArray();
	}

	ApiReplay& Session::Side(const JsonValue& params) {
		return params["side"].Str() == "b" ? Other() : *replay_;
	}

	Answer Session::Handle(const std::string& method, const JsonValue& params, const Progress& progress) {
		Answer answer;
		JsonWriter out;
		out.BeginObject();
		try {
			if (method == "initialize") Initialize(params, out);
			else if (!replay_) Fail("no capture is open: send initialize first");
			else if (method == "events") Side(params).Events(out);
			else if (method == "apiCalls") Side(params).ApiCalls((u32)params["eid"].Uint(0), out);
			else if (method == "state") Side(params).State((u32)params["eid"].Uint(0), out);
			else if (method == "resources") Side(params).Resources(out);
			else if (method == "texture") Side(params).Texture(params, out, answer.binary);
			else if (method == "png") Png(params, out);
			else if (method == "pixel") Side(params).Pixel(params, out);
			else if (method == "pixelHistory") Side(params).PixelHistory(params, out);
			else if (method == "debugShader") Side(params).DebugShader(params, out);
			else if (method == "buffer") Side(params).Buffer(params, out, answer.binary);
			else if (method == "mesh") Side(params).Mesh(params, out, answer.binary);
			else if (method == "shader") Side(params).Shader(params, out);
			else if (method == "timing") Side(params).Timing(params, out, progress);
			else if (method == "experiments") Side(params).Experiments(params, out, progress);
			else if (method == "messages") Side(params).Messages(out);
			else if (method == "compareOpen") CompareOpen(params, out);
			else if (method == "compareClose") other_.reset(), otherFile_.reset();
			else if (method == "shaderSource") replay_->ShaderSource(params, out);
			else if (method == "editShader") replay_->EditShader(params, out);
			else if (method == "revertShader") replay_->RevertShader(params, out);
			else if (method == "edits") replay_->Edits(out);
			else if (method == "editDiff") EditDiff(params, out, answer.binary);
			else if (method == "compareEvents") CompareEvents(out);
			else if (method == "compareState") CompareState(params, out);
			else if (method == "compareTexture") CompareTexture(params, out, answer.binary);
			else if (method == "compareTiming") CompareTiming(params, out, progress);
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
