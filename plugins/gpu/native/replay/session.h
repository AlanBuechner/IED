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
		bool SameApi() const { return otherFile_ && file_->GetApi() == otherFile_->GetApi(); }

		std::unique_ptr<CaptureReader> file_;
		std::unique_ptr<ApiReplay> replay_;
		std::unique_ptr<CaptureReader> otherFile_;
		std::unique_ptr<ApiReplay> other_;
	};
}
