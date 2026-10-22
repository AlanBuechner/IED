#include "Server/DapServer.h"
#include "Json/JsonWriter.h"
#include <filesystem>

namespace C4DAP::Server
{
	namespace
	{
		Json::Value Text(const std::string& text) { return Json::Value::String(text); }

		const char* StopReasonName(Target::StopReason reason)
		{
			switch (reason)
			{
			case Target::StopReason::Entry: return "entry";
			case Target::StopReason::Breakpoint: return "breakpoint";
			case Target::StopReason::Step: return "step";
			case Target::StopReason::Pause: return "pause";
			case Target::StopReason::Exception: return "exception";
			}
			return "pause";
		}

		Json::Value ExceptionFilter(const char* filter, const char* label, bool on)
		{
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("filter", Text(filter));
			entry.SetMember("label", Text(label));
			entry.SetMember("default", Json::Value::Bool(on));
			return entry;
		}

		Json::Value Capabilities()
		{
			Json::Value body = Json::Value::MakeObject();
			for (const char* yes : { "supportsConfigurationDoneRequest", "supportsEvaluateForHovers", "supportsTerminateRequest" })
				body.SetMember(yes, Json::Value::Bool(true));
			for (const char* no : { "supportsSetVariable", "supportsConditionalBreakpoints", "supportsHitConditionalBreakpoints", "supportsLogPoints",
				"supportsRestartRequest", "supportsStepBack", "supportsDataBreakpoints", "supportsModulesRequest", "supportsLoadedSourcesRequest",
				"supportsGotoTargetsRequest", "supportsFunctionBreakpoints" })
				body.SetMember(no, Json::Value::Bool(false));
			Json::Value filters = Json::Value::MakeArray();
			filters.Append(ExceptionFilter("thrown", "Thrown exceptions", false));
			filters.Append(ExceptionFilter("uncaught", "Uncaught exceptions", true));
			body.SetMember("exceptionBreakpointFilters", filters);
			return body;
		}
	}

	DapServer::DapServer(Send send) : m_Send(std::move(send)), m_Debuggee(std::make_unique<Target::Debuggee>(EventsToClient())) {}

	DapServer::~DapServer()
	{
		m_Debuggee.reset(); // ends the program before the server it reports to
	}

	const std::unordered_map<std::string, DapServer::Handler>& DapServer::Handlers()
	{
		static const std::unordered_map<std::string, Handler> handlers = {
			{ "initialize", &DapServer::Initialize }, { "launch", &DapServer::Launch }, { "configurationDone", &DapServer::ConfigurationDone },
			{ "disconnect", &DapServer::Disconnect }, { "terminate", &DapServer::Terminate }, { "setBreakpoints", &DapServer::SetBreakpoints },
			{ "setExceptionBreakpoints", &DapServer::SetExceptionBreakpoints }, { "continue", &DapServer::Continue }, { "next", &DapServer::Next },
			{ "stepIn", &DapServer::StepIn }, { "stepOut", &DapServer::StepOut }, { "pause", &DapServer::Pause }, { "threads", &DapServer::Threads },
			{ "stackTrace", &DapServer::StackTrace }, { "scopes", &DapServer::Scopes }, { "variables", &DapServer::Variables },
			{ "evaluate", &DapServer::Evaluate },
		};
		return handlers;
	}

	void DapServer::HandleMessage(const Json::Value& message)
	{
		if (message.Member("type").GetString() != "request") return;
		const std::string command = message.Member("command").GetString();
		const auto handler = Handlers().find(command);
		if (handler == Handlers().end())
		{
			Fail(message, "C4DAP does not support the request " + command);
			return;
		}
		(this->*handler->second)(message);
	}

	void DapServer::Write(Json::Value message)
	{
		std::lock_guard<std::mutex> lock(m_SendLock); // sequence numbers go out in order
		message.SetMember("seq", Json::Value::Int(m_NextSeq++));
		m_Send(Json::Writer::Encode(message));
	}

	void DapServer::Respond(const Json::Value& request, const Json::Value& body)
	{
		Json::Value response = Json::Value::MakeObject();
		response.SetMember("type", Text("response"));
		response.SetMember("request_seq", Json::Value::Int(request.Member("seq").GetInt()));
		response.SetMember("success", Json::Value::Bool(true));
		response.SetMember("command", request.Member("command"));
		response.SetMember("body", body);
		Write(response);
	}

	void DapServer::Fail(const Json::Value& request, const std::string& message)
	{
		Json::Value response = Json::Value::MakeObject();
		response.SetMember("type", Text("response"));
		response.SetMember("request_seq", Json::Value::Int(request.Member("seq").GetInt()));
		response.SetMember("success", Json::Value::Bool(false));
		response.SetMember("command", request.Member("command"));
		response.SetMember("message", Text(message));
		Write(response);
	}

	void DapServer::Emit(const std::string& event, const Json::Value& body)
	{
		Json::Value message = Json::Value::MakeObject();
		message.SetMember("type", Text("event"));
		message.SetMember("event", Text(event));
		message.SetMember("body", body);
		Write(message);
	}

	Target::EngineEvents DapServer::EventsToClient()
	{
		Target::EngineEvents events;
		events.stopped = [this](const Target::StopInfo& stop) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("reason", Text(StopReasonName(stop.reason)));
			body.SetMember("threadId", Json::Value::Int(stop.thread));
			body.SetMember("allThreadsStopped", Json::Value::Bool(true));
			if (!stop.description.empty())
			{
				body.SetMember("description", Text(stop.description));
				body.SetMember("text", Text(stop.description));
			}
			if (!stop.breakpoints.empty())
			{
				Json::Value ids = Json::Value::MakeArray();
				for (uint32_t id : stop.breakpoints) ids.Append(Json::Value::Int(id));
				body.SetMember("hitBreakpointIds", ids);
			}
			Emit("stopped", body);
		};
		events.thread = [this](DWORD thread, bool started) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("reason", Text(started ? "started" : "exited"));
			body.SetMember("threadId", Json::Value::Int(thread));
			Emit("thread", body);
		};
		events.breakpointChanged = [this](const Target::BoundBreakpoint& breakpoint) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("reason", Text("changed"));
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("id", Json::Value::Int(breakpoint.id));
			entry.SetMember("verified", Json::Value::Bool(breakpoint.verified));
			entry.SetMember("line", Json::Value::Int(breakpoint.line));
			if (!breakpoint.message.empty()) entry.SetMember("message", Text(breakpoint.message));
			body.SetMember("breakpoint", entry);
			Emit("breakpoint", body);
		};
		events.output = [this](const char* category, const std::string& text) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("category", Text(category));
			body.SetMember("output", Text(text));
			Emit("output", body);
		};
		events.exited = [this](uint32_t exitCode) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("exitCode", Json::Value::Int(exitCode));
			Emit("exited", body);
			Emit("terminated", Json::Value::MakeObject());
		};
		events.processStarted = [this](DWORD processId) {
			Json::Value body = Json::Value::MakeObject();
			body.SetMember("name", Text(std::filesystem::path(m_Program).filename().string()));
			body.SetMember("systemProcessId", Json::Value::Int(processId));
			body.SetMember("isLocalProcess", Json::Value::Bool(true));
			body.SetMember("startMethod", Text("launch"));
			Emit("process", body);
		};
		return events;
	}

	void DapServer::Initialize(const Json::Value& request)
	{
		Respond(request, Capabilities());
		Emit("initialized", Json::Value::MakeObject());
	}

	void DapServer::Launch(const Json::Value& request)
	{
		const Json::Value arguments = request.Member("arguments");
		Target::LaunchOptions options;
		options.program = arguments.Member("program").GetString();
		options.cwd = arguments.Member("cwd").GetString();
		options.stopOnEntry = arguments.Member("stopOnEntry").GetBool();
		const Json::Value args = arguments.Member("args");
		for (uint32_t i = 0; i < args.Size(); ++i) options.args.push_back(args.At(i).GetString());
		if (options.program.empty())
		{
			Fail(request, "launch needs the program to debug (\"program\")");
			return;
		}
		if (options.cwd.empty()) options.cwd = std::filesystem::path(options.program).parent_path().string();
		m_Program = options.program;
		const std::string failure = m_Debuggee->Call([&options](Target::Engine& engine) { return engine.Launch(options); });
		if (!failure.empty())
		{
			Fail(request, failure);
			return;
		}
		Respond(request, Json::Value::MakeObject());
	}

	void DapServer::ConfigurationDone(const Json::Value& request)
	{
		m_Debuggee->Call([this, &request](Target::Engine& engine) {
			Respond(request, Json::Value::MakeObject());
			engine.Configured();
		});
	}

	void DapServer::Disconnect(const Json::Value& request)
	{
		m_Debuggee->Call([](Target::Engine& engine) { engine.Terminate(); });
		Respond(request, Json::Value::MakeObject());
		m_Exit = true;
	}

	void DapServer::Terminate(const Json::Value& request)
	{
		m_Debuggee->Call([this, &request](Target::Engine& engine) {
			Respond(request, Json::Value::MakeObject());
			engine.Terminate();
		});
	}
} // namespace C4DAP::Server
