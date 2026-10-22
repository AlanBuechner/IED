// session.h - one open capture in the replayer: `initialize` opens it and picks the API's replay, the other methods of
// ied-gpu/1 (docs/22 "Replay") go to that replay. `compareOpen` opens a second capture beside it (docs/22 "Compare"):
// the compare methods answer from both, any other method reaches the second with "side": "b", and `compareClose` lets
// it go. A failing request answers with its message; the session goes on.
#pragma once

#include "replay.h"

namespace fb::replay
{
	struct Answer
	{
		std::string result;          // JSON of the result object, when it succeeded
		std::string error;           // the message, when it failed
		std::vector<u8> binary;
	};

	// A replay's answer, parsed.
	template <class Call> JsonValue Parsed(Call call) {
		JsonWriter writer;
		writer.BeginObject();
		call(writer);
		writer.EndObject();
		JsonValue value;
		std::string error;
		if (!JsonValue::Parse(writer.Text().data(), writer.Text().size(), value, error)) Fail("a replay's answer does not parse: %s", error.c_str());
		return value;
	}

	// Request params from a writer holding one object.
	inline JsonValue Params(const JsonWriter& writer) {
		JsonValue value;
		std::string error;
		JsonValue::Parse(writer.Text().data(), writer.Text().size(), value, error);
		return value;
	}

	// The first render target a state answer names; 0 when it names none.
	u64 FirstTarget(const JsonValue& state);
	// Two texture answers of one size and their RGBA32F pixels, diffed texel by texel (compare.h) into `out` as an image
	// the viewer shows (`view` names it), with the changed pixels, the largest and mean difference and their box, then
	// the two answers as `a` and `b`; the difference image to `binary`.
	void WriteDifference(const JsonValue& params, const JsonValue& a, const std::vector<u8>& pixelsA, const JsonValue& b, const std::vector<u8>& pixelsB, const char* view,
		JsonWriter& out, std::vector<u8>& binary);

	class Session
	{
	public:
		Answer Handle(const std::string& method, const JsonValue& params, const Progress& progress);
		bool Open() const { return replay_ != nullptr; }

	private:
		void Initialize(const JsonValue& params, JsonWriter& out);
		void Png(const JsonValue& params, JsonWriter& out);
		// The replay a request addresses: the second capture's with "side": "b".
		ApiReplay& Side(const JsonValue& params);

		// sessioncompare.cpp
		void CompareOpen(const JsonValue& params, JsonWriter& out);
		void CompareEvents(JsonWriter& out);
		void CompareState(const JsonValue& params, JsonWriter& out);
		void CompareTexture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary);
		void CompareTiming(const JsonValue& params, JsonWriter& out, const Progress& progress);
		ApiReplay& Other();
		// sessionedit.cpp: the event's output with the captured shaders against the edits.
		void EditDiff(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary);
		bool SameApi() const { return otherFile_ && file_->GetApi() == otherFile_->GetApi(); }

		std::unique_ptr<CaptureReader> file_;
		std::unique_ptr<ApiReplay> replay_;
		std::unique_ptr<CaptureReader> otherFile_;
		std::unique_ptr<ApiReplay> other_;
	};
}
