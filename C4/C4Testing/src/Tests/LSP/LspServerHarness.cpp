#include "LspServerHarness.h"
#include "LspTestSupport.h"
#include "Protocol/LspTypes.h"
#include <fstream>

namespace Testing::Lsp
{
	using Json::Value;

	namespace
	{
		Value Message(const std::string& method, const Value& params)
		{
			Value message = Value::MakeObject();
			message.SetMember("jsonrpc", Value::String(Core::String("2.0")));
			message.SetMember("method", Value::String(Core::String(method)));
			message.SetMember("params", params);
			return message;
		}
	}

	ServerHarness::ServerHarness(const std::string& rootUri) : m_RootUri(rootUri)
	{
		m_Server = std::make_unique<C4LSP::Server::LspServer>([this](const std::string& framed) {
			std::lock_guard<std::mutex> lock(m_Lock);
			m_Framed.push_back(framed);
		});
	}

	void ServerHarness::Initialize(const Value& options)
	{
		Value params = Value::MakeObject();
		params.SetMember("rootUri", Value::String(Core::String(m_RootUri)));
		params.SetMember("initializationOptions", options);
		Request("initialize", params);
		Notify("initialized", Value::Null());
		m_Server->WaitForCurrentWork();
	}

	void ServerHarness::Open(const std::string& uri, const std::string& text)
	{
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		document.SetMember("languageId", Value::String(Core::String("c4")));
		document.SetMember("version", Value::Int(1));
		document.SetMember("text", Value::String(Core::String(text)));
		Value params = Value::MakeObject();
		params.SetMember("textDocument", document);
		Notify("textDocument/didOpen", params);
		m_Server->WaitForCurrentWork();
	}

	void ServerHarness::Close(const std::string& uri)
	{
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		Value params = Value::MakeObject();
		params.SetMember("textDocument", document);
		Notify("textDocument/didClose", params);
		m_Server->WaitForCurrentWork();
	}

	void ServerHarness::Notify(const std::string& method, const Value& params)
	{
		m_Server->HandleMessage(Message(method, params));
	}

	Value ServerHarness::Request(const std::string& method, const Value& params)
	{
		const int64 id = m_NextId++;
		Value message = Message(method, params);
		message.SetMember("id", Value::Int(id));
		m_Server->HandleMessage(message);
		for (const Value& answer : Messages())
			if (answer.HasMember("id") && !answer.HasMember("method") && answer.Member("id").GetInt() == id)
				return answer.Member("result");
		return Value::Null();
	}

	std::string ServerHarness::MessagesFor(const std::string& uri)
	{
		m_Server->WaitForCurrentWork();
		std::string messages;
		for (const Value& message : Messages())
		{
			if (message.Member("method").GetString() != "textDocument/publishDiagnostics" || message.Member("params").Member("uri").GetString() != uri) continue;
			messages.clear(); // the last publish is what the editor shows
			const Value diagnostics = message.Member("params").Member("diagnostics");
			for (uint32 i = 0; i < diagnostics.Size(); ++i) messages += std::string(diagnostics.At(i).Member("message").GetString()) + "\n";
		}
		return messages;
	}

	Value ServerHarness::At(const std::string& uri, const std::string& text, const std::string& needle)
	{
		const C4LSP::Protocol::Position position = PositionOf(text, needle);
		Value document = Value::MakeObject();
		document.SetMember("uri", Value::String(Core::String(uri)));
		Value at = Value::MakeObject();
		at.SetMember("line", Value::Int(position.line));
		at.SetMember("character", Value::Int(position.character));
		Value params = Value::MakeObject();
		params.SetMember("textDocument", document);
		params.SetMember("position", at);
		return params;
	}

	std::vector<Value> ServerHarness::Messages()
	{
		std::vector<std::string> framed;
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			framed = m_Framed;
		}
		std::vector<Value> messages;
		for (const std::string& frame : framed)
		{
			const size_t body = frame.find("\r\n\r\n");
			if (body != std::string::npos) messages.push_back(Value(Core::LoadObjectDescriptionFromJsonString(frame.substr(body + 4))));
		}
		return messages;
	}

	void WriteFile(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << text;
	}

	std::string FileUri(const std::filesystem::path& path) { return C4LSP::Protocol::DocumentUri::ToUri(path.generic_string()); }
} // namespace Testing::Lsp
