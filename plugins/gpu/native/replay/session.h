// session.h - one open capture in the replayer: `initialize` opens it and picks the API's replay, the other methods of
// ied-gpu/1 (docs/22 "Replay") go to that replay. A failing request answers with its message; the session goes on.
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

		std::unique_ptr<CaptureReader> file_;
		std::unique_ptr<ApiReplay> replay_;
	};
}
