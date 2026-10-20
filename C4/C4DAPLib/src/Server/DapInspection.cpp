#include "Server/DapServer.h"
#include <filesystem>

namespace C4DAP::Server
{
	namespace
	{
		Json::Value VariableBody(const Target::VariableInfo& variable)
		{
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("name", Json::Value::String(variable.name));
			entry.SetMember("value", Json::Value::String(variable.value));
			entry.SetMember("type", Json::Value::String(variable.type));
			entry.SetMember("variablesReference", Json::Value::Int(variable.reference));
			return entry;
		}

		Json::Value FrameBody(const Target::FrameInfo& frame)
		{
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("id", Json::Value::Int(frame.id));
			entry.SetMember("name", Json::Value::String(frame.name));
			entry.SetMember("line", Json::Value::Int(frame.position.line));
			// A frame whose source is not on this machine (the C runtime's, built elsewhere) shows as one without.
			std::error_code ignored;
			const bool hasSource = frame.position.Known() && std::filesystem::exists(frame.position.path, ignored);
			entry.SetMember("column", Json::Value::Int(hasSource ? 1 : 0));
			if (hasSource)
			{
				Json::Value source = Json::Value::MakeObject();
				source.SetMember("name", Json::Value::String(std::filesystem::path(frame.position.path).filename().string()));
				source.SetMember("path", Json::Value::String(frame.position.path));
				entry.SetMember("source", source);
			}
			else entry.SetMember("presentationHint", Json::Value::String("subtle")); // code without source: the runtime, the system
			return entry;
		}
	}

	void DapServer::Threads(const Json::Value& request)
	{
		const std::vector<Target::ThreadInfo> threads = m_Debuggee->Call([](Target::Engine& engine) { return engine.Threads(); });
		Json::Value list = Json::Value::MakeArray();
		for (const Target::ThreadInfo& thread : threads)
		{
			Json::Value entry = Json::Value::MakeObject();
			entry.SetMember("id", Json::Value::Int(thread.id));
			entry.SetMember("name", Json::Value::String(thread.name));
			list.Append(entry);
		}
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("threads", list);
		Respond(request, body);
	}

	void DapServer::StackTrace(const Json::Value& request)
	{
		const Json::Value arguments = request.Member("arguments");
		const DWORD thread = static_cast<DWORD>(arguments.Member("threadId").GetInt());
		const uint32_t start = static_cast<uint32_t>(arguments.Member("startFrame").GetInt());
		const uint32_t levels = static_cast<uint32_t>(arguments.Member("levels").GetInt());
		uint32_t total = 0;
		const std::vector<Target::FrameInfo> frames =
			m_Debuggee->Call([thread, start, levels, &total](Target::Engine& engine) { return engine.StackTrace(thread, start, levels, total); });
		Json::Value list = Json::Value::MakeArray();
		for (const Target::FrameInfo& frame : frames) list.Append(FrameBody(frame));
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("stackFrames", list);
		body.SetMember("totalFrames", Json::Value::Int(total));
		Respond(request, body);
	}

	void DapServer::Scopes(const Json::Value& request)
	{
		const uint32_t frame = static_cast<uint32_t>(request.Member("arguments").Member("frameId").GetInt());
		const uint32_t locals = m_Debuggee->Call([frame](Target::Engine& engine) { return engine.LocalsOf(frame); });
		Json::Value list = Json::Value::MakeArray();
		if (locals != 0)
		{
			Json::Value scope = Json::Value::MakeObject();
			scope.SetMember("name", Json::Value::String("Locals"));
			scope.SetMember("presentationHint", Json::Value::String("locals"));
			scope.SetMember("variablesReference", Json::Value::Int(locals));
			scope.SetMember("expensive", Json::Value::Bool(false));
			list.Append(scope);
		}
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("scopes", list);
		Respond(request, body);
	}

	void DapServer::Variables(const Json::Value& request)
	{
		const uint32_t reference = static_cast<uint32_t>(request.Member("arguments").Member("variablesReference").GetInt());
		const std::vector<Target::VariableInfo> variables = m_Debuggee->Call([reference](Target::Engine& engine) { return engine.Variables(reference); });
		Json::Value list = Json::Value::MakeArray();
		for (const Target::VariableInfo& variable : variables) list.Append(VariableBody(variable));
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("variables", list);
		Respond(request, body);
	}

	void DapServer::Evaluate(const Json::Value& request)
	{
		const Json::Value arguments = request.Member("arguments");
		const std::string expression = arguments.Member("expression").GetString();
		const uint32_t frame = static_cast<uint32_t>(arguments.Member("frameId").GetInt());
		const Target::EvaluateResult result =
			m_Debuggee->Call([&expression, frame](Target::Engine& engine) { return engine.Evaluate(expression, frame); });
		if (!result.error.empty())
		{
			Fail(request, result.error);
			return;
		}
		Json::Value body = Json::Value::MakeObject();
		body.SetMember("result", Json::Value::String(result.value.value));
		body.SetMember("type", Json::Value::String(result.value.type));
		body.SetMember("variablesReference", Json::Value::Int(result.value.reference));
		Respond(request, body);
	}
} // namespace C4DAP::Server
