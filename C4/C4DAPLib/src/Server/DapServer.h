#pragma once
#include "Json/JsonValue.h"
#include "Target/Debuggee.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace C4DAP::Server
{
	/// The Debug Adapter Protocol side of C4DAP (docs/tools/03-editor-lsp-dap.md): turns requests into calls on the
	/// debugger and its events into DAP events. Messages go out through `send`, framed, from any thread.
	class DapServer
	{
	public:
		using Send = std::function<void(const std::string& framed)>;

		explicit DapServer(Send send);
		~DapServer();

		void HandleMessage(const Json::Value& message);
		/// After `disconnect`: the adapter's work is done.
		bool ShouldExit() const { return m_Exit; }

	private:
		using Handler = void (DapServer::*)(const Json::Value& request);

		void Respond(const Json::Value& request, const Json::Value& body);
		void Fail(const Json::Value& request, const std::string& message);
		void Emit(const std::string& event, const Json::Value& body);
		void Write(Json::Value message);
		Target::EngineEvents EventsToClient();

		// Lifecycle (DapServer.cpp)
		void Initialize(const Json::Value& request);
		void Launch(const Json::Value& request);
		void ConfigurationDone(const Json::Value& request);
		void Disconnect(const Json::Value& request);
		void Terminate(const Json::Value& request);
		// Breakpoints and execution (DapExecution.cpp)
		void SetBreakpoints(const Json::Value& request);
		void SetExceptionBreakpoints(const Json::Value& request);
		void Continue(const Json::Value& request);
		void Next(const Json::Value& request);
		void StepIn(const Json::Value& request);
		void StepOut(const Json::Value& request);
		void Pause(const Json::Value& request);
		void StepThread(const Json::Value& request, Target::StepKind kind);
		// Inspection (DapInspection.cpp)
		void Threads(const Json::Value& request);
		void StackTrace(const Json::Value& request);
		void Scopes(const Json::Value& request);
		void Variables(const Json::Value& request);
		void Evaluate(const Json::Value& request);

		static const std::unordered_map<std::string, Handler>& Handlers();

		Send m_Send;
		std::mutex m_SendLock;
		int64_t m_NextSeq = 1;
		bool m_Exit = false;
		std::unique_ptr<Target::Debuggee> m_Debuggee;
	};
} // namespace C4DAP::Server
