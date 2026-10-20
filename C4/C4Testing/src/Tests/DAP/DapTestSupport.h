#pragma once
// Shared helpers for the debug adapter tests: plain utilities (no reflection), so the reflection generator does
// not take them for test classes.
#include "Json/JsonValue.h"
#include "Server/DapServer.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Testing::Dap
{
	/// A C4 program built with `C4.exe -g` for a debug session.
	struct DebugProgram
	{
		std::filesystem::path exe;
		std::filesystem::path source;
		bool built = false;
	};

	/// Builds `source` as main.cf of its own folder (under the temp folder, named `name`).
	DebugProgram BuildProgram(const std::string& name, const std::string& source);

	/// The 1-based line of the first line of `source` that contains `marker`; 0 when none does.
	uint32_t LineOf(const std::string& source, const std::string& marker);

	/// A DAP client of an in-process DapServer: a request waits for its response; events queue up in order.
	class DapClient
	{
	public:
		DapClient();
		~DapClient();

		/// The response to `command` (null after a timeout).
		Json::Value Request(const std::string& command, const Json::Value& arguments = Json::Value::MakeObject());
		/// The next event called `name`; the events before it are passed over (they stay in the history).
		Json::Value WaitEvent(const std::string& name);
		/// Every event called `name` so far.
		std::vector<Json::Value> History(const std::string& name);

		/// initialize, launch, a breakpoint on each of `lines`, configurationDone: the first stop (null when the
		/// program never stopped).
		Json::Value Start(const DebugProgram& program, const std::vector<uint32_t>& lines, bool stopOnEntry = false);
		/// The top frame of `thread` after a stop: its function, line and frame id.
		Json::Value TopFrame(int64 thread);
		/// The Locals of a frame, by name: name to the variable's value text.
		std::vector<std::pair<std::string, Json::Value>> Locals(int64 frameId);
		/// The value text of `expression` in the frame (or the failure's message, prefixed "error: ").
		std::string Evaluate(const std::string& expression, int64 frameId);

	private:
		void Receive(const std::string& framed);
		bool WaitFor(const std::function<bool()>& ready);

		std::mutex m_Lock;
		std::condition_variable m_Arrived;
		std::vector<Json::Value> m_Responses;
		std::deque<Json::Value> m_Pending;
		std::vector<Json::Value> m_Events;
		int64 m_Seq = 0;
		std::unique_ptr<C4DAP::Server::DapServer> m_Server;
	};

	/// A JSON object with the given string, integer or bool members.
	Json::Value Object(std::initializer_list<std::pair<const char*, Json::Value>> members);
	Json::Value Str(const std::string& text);
	Json::Value Num(int64 value);
} // namespace Testing::Dap
