#include "Server/DapServer.h"

namespace C4DAP::Server
{
	namespace
	{
		DWORD ThreadOf(const Json::Value& request)
		{
			return static_cast<DWORD>(request.Member("arguments").Member("threadId").GetInt());
		}

		Json::Value BreakpointBody(const Target::BoundBreakpoint& breakpoint)
		{
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("id", Json::Value::Int(breakpoint.id));
			entry.SetMember("verified", Json::Value::Bool(breakpoint.verified));
			entry.SetMember("line", Json::Value::Int(breakpoint.line));
			if (!breakpoint.message.empty()) entry.SetMember("message", Json::Value::String(breakpoint.message));
			return entry;
		}
	}

	void DapServer::SetBreakpoints(const Json::Value& request)
	{
		const Json::Value arguments = request.Member("arguments");
		const std::string path = arguments.Member("source").Member("path").GetString();
		if (path.empty())
		{
			Fail(request, "setBreakpoints needs the source's path");
			return;
		}
		std::vector<uint32_t> lines;
		const Json::Value breakpoints = arguments.Member("breakpoints");
		for (uint32_t i = 0; i < breakpoints.Size(); ++i) lines.push_back(static_cast<uint32_t>(breakpoints.At(i).Member("line").GetInt()));
		const std::vector<Target::BoundBreakpoint> bound =
			m_Debuggee->Call([&path, &lines](Target::Engine& engine) { return engine.SetBreakpoints(path, lines); });
		Json::Value list = Json::Value::MakeArray();
		for (const Target::BoundBreakpoint& breakpoint : bound) list.Append(BreakpointBody(breakpoint));
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("breakpoints", list);
		Respond(request, body);
	}

	void DapServer::SetExceptionBreakpoints(const Json::Value& request)
	{
		bool thrown = false, uncaught = false;
		const Json::Value filters = request.Member("arguments").Member("filters");
		for (uint32_t i = 0; i < filters.Size(); ++i)
		{
			const std::string filter = filters.At(i).GetString();
			thrown = thrown || filter == "thrown";
			uncaught = uncaught || filter == "uncaught";
		}
		m_Debuggee->Call([thrown, uncaught](Target::Engine& engine) { engine.SetExceptionFilters(thrown, uncaught); });
		Respond(request, Json::Value::MakeObject());
	}

	// The response goes out before the program runs on, so no event of the run can overtake it.
	void DapServer::Continue(const Json::Value& request)
	{
		m_Debuggee->Call([this, &request](Target::Engine& engine) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("allThreadsContinued", Json::Value::Bool(true));
			Respond(request, body);
			engine.Continue();
		});
	}

	void DapServer::StepThread(const Json::Value& request, Target::StepKind kind)
	{
		const DWORD thread = ThreadOf(request);
		m_Debuggee->Call([this, &request, kind, thread](Target::Engine& engine) {
			Respond(request, Json::Value::MakeObject());
			engine.Step(kind, thread);
		});
	}

	void DapServer::Next(const Json::Value& request) { StepThread(request, Target::StepKind::Over); }
	void DapServer::StepIn(const Json::Value& request) { StepThread(request, Target::StepKind::Into); }
	void DapServer::StepOut(const Json::Value& request) { StepThread(request, Target::StepKind::Out); }

	void DapServer::Pause(const Json::Value& request)
	{
		m_Debuggee->Call([this, &request](Target::Engine& engine) {
			Respond(request, Json::Value::MakeObject());
			engine.Pause();
		});
	}
} // namespace C4DAP::Server
