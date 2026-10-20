#include "DapTestSupport.h"
#include "Compiler/Toolchain.h"
#include "Json/JsonReader.h"
#include "Target/Launch.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace Testing::Dap
{
	namespace fs = std::filesystem;

	namespace
	{
		constexpr auto kReplyWait = std::chrono::seconds(30);
		constexpr DWORD kBuildMilliseconds = 120000;

		// The compiler of this build: C4Testing runs from bin/<config>/C4Testing, C4.exe is in bin/<config>/C4.
		fs::path Compiler() { return Scripting::Toolchain::Home().parent_path() / "C4" / "C4.exe"; }

		bool Run(const std::wstring& commandLine)
		{
			STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
			PROCESS_INFORMATION started{};
			std::wstring line = commandLine;
			if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started)) return false;
			WaitForSingleObject(started.hProcess, kBuildMilliseconds);
			DWORD exitCode = 1;
			GetExitCodeProcess(started.hProcess, &exitCode);
			CloseHandle(started.hThread);
			CloseHandle(started.hProcess);
			return exitCode == 0;
		}
	}

	DebugProgram BuildProgram(const std::string& name, const std::string& source)
	{
		const fs::path folder = fs::temp_directory_path() / "c4dap_tests" / name;
		std::error_code ignored;
		fs::remove_all(folder, ignored);
		fs::create_directories(folder);
		DebugProgram program;
		program.source = folder / "main.cf";
		program.exe = folder / "app.exe";
		std::ofstream(program.source, std::ios::binary) << source;
		program.built = Run(C4DAP::Target::CommandLine(Compiler().string(), { folder.string(), "-g", "-o", program.exe.string() }));
		return program;
	}

	uint32_t LineOf(const std::string& source, const std::string& marker)
	{
		std::istringstream lines(source);
		std::string line;
		for (uint32_t number = 1; std::getline(lines, line); ++number)
			if (line.find(marker) != std::string::npos) return number;
		return 0;
	}

	Json::Value Str(const std::string& text) { return Json::Value::String(text); }
	Json::Value Num(int64 value) { return Json::Value::Int(value); }

	Json::Value Object(std::initializer_list<std::pair<const char*, Json::Value>> members)
	{
		Json::Value object = Json::Value::MakeObject();
		for (const auto& [key, value] : members) object.SetMember(key, value);
		return object;
	}

	DapClient::DapClient() : m_Server(std::make_unique<C4DAP::Server::DapServer>([this](const std::string& framed) { Receive(framed); })) {}

	DapClient::~DapClient()
	{
		m_Server.reset();
	}

	void DapClient::Receive(const std::string& framed)
	{
		Json::Reader reader;
		reader.Append(framed.data(), framed.size());
		Json::Value message;
		if (!reader.TryReadDocument(message)) return;
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			if (message.Member("type").GetString() == "response") m_Responses.push_back(message);
			else
			{
				m_Pending.push_back(message);
				m_Events.push_back(message);
			}
		}
		m_Arrived.notify_all();
	}

	bool DapClient::WaitFor(const std::function<bool()>& ready)
	{
		std::unique_lock<std::mutex> lock(m_Lock);
		return m_Arrived.wait_for(lock, kReplyWait, ready);
	}

	Json::Value DapClient::Request(const std::string& command, const Json::Value& arguments)
	{
		const int64 seq = ++m_Seq;
		m_Server->HandleMessage(Object({ { "seq", Num(seq) }, { "type", Str("request") }, { "command", Str(command) }, { "arguments", arguments } }));
		Json::Value response = Json::Value::Null();
		WaitFor([&] {
			for (const Json::Value& candidate : m_Responses)
				if (candidate.Member("request_seq").GetInt() == seq) { response = candidate; return true; }
			return false;
		});
		return response;
	}

	Json::Value DapClient::WaitEvent(const std::string& name)
	{
		Json::Value event = Json::Value::Null();
		WaitFor([&] {
			while (!m_Pending.empty())
			{
				Json::Value next = m_Pending.front();
				m_Pending.pop_front();
				if (next.Member("event").GetString() == name) { event = next; return true; }
			}
			return false;
		});
		return event;
	}

	std::vector<Json::Value> DapClient::History(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(m_Lock);
		std::vector<Json::Value> found;
		for (const Json::Value& event : m_Events)
			if (event.Member("event").GetString() == name) found.push_back(event);
		return found;
	}

	Json::Value DapClient::Start(const DebugProgram& program, const std::vector<uint32_t>& lines, bool stopOnEntry)
	{
		Request("initialize", Object({ { "adapterID", Str("c4") } }));
		if (!Request("launch", Object({ { "program", Str(program.exe.string()) }, { "stopOnEntry", Json::Value::Bool(stopOnEntry) } })).Member("success").GetBool())
			return Json::Value::Null();
		Json::Value breakpoints = Json::Value::MakeArray();
		for (uint32_t line : lines) breakpoints.Append(Object({ { "line", Num(line) } }));
		Request("setBreakpoints", Object({ { "source", Object({ { "path", Str(program.source.string()) } }) }, { "breakpoints", breakpoints } }));
		Request("configurationDone");
		return WaitEvent("stopped");
	}

	Json::Value DapClient::TopFrame(int64 thread)
	{
		return Request("stackTrace", Object({ { "threadId", Num(thread) } })).Member("body").Member("stackFrames").At(0);
	}

	std::vector<std::pair<std::string, Json::Value>> DapClient::Locals(int64 frameId)
	{
		const Json::Value scopes = Request("scopes", Object({ { "frameId", Num(frameId) } })).Member("body").Member("scopes");
		const int64 reference = scopes.At(0).Member("variablesReference").GetInt();
		const Json::Value variables = Request("variables", Object({ { "variablesReference", Num(reference) } })).Member("body").Member("variables");
		std::vector<std::pair<std::string, Json::Value>> named;
		for (uint32_t i = 0; i < variables.Size(); ++i) named.emplace_back(variables.At(i).Member("name").GetString(), variables.At(i));
		return named;
	}

	std::string DapClient::Evaluate(const std::string& expression, int64 frameId)
	{
		const Json::Value response = Request("evaluate", Object({ { "expression", Str(expression) }, { "frameId", Num(frameId) } }));
		if (!response.Member("success").GetBool()) return "error: " + response.Member("message").GetString();
		return response.Member("body").Member("result").GetString();
	}
} // namespace Testing::Dap
