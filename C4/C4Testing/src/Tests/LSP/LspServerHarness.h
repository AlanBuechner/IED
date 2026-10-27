#pragma once
// A language server driven over its message API with its output captured, for the tests that talk to the whole server.
// A plain utility (no reflection), so the reflection generator does not take it for a test class.
#include "Server/LspServer.h"
#include "Json/JsonValue.h"
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Testing::Lsp
{
	class ServerHarness
	{
	public:
		explicit ServerHarness(const std::string& rootUri);

		// initialize with `options` as initializationOptions, then initialized; returns once the server is idle.
		void Initialize(const Json::Value& options);
		// didOpen of `uri` with `text`; returns once its analysis is published.
		void Open(const std::string& uri, const std::string& text);
		void Close(const std::string& uri);
		void Notify(const std::string& method, const Json::Value& params);
		// A request's result (null when it failed). Requests are answered on the calling thread.
		Json::Value Request(const std::string& method, const Json::Value& params);
		// The last diagnostics published for `uri`, one message per line; empty when there were none.
		std::string MessagesFor(const std::string& uri);
		// {textDocument: {uri}, position: {line, character}} at the first `needle` of `text`.
		static Json::Value At(const std::string& uri, const std::string& text, const std::string& needle);

		C4LSP::Server::LspServer& Server() { return *m_Server; }

	private:
		std::vector<Json::Value> Messages();

		std::string m_RootUri;
		std::mutex m_Lock;
		std::vector<std::string> m_Framed; // the server's output, frame by frame (the worker writes too)
		std::unique_ptr<C4LSP::Server::LspServer> m_Server;
		int64 m_NextId = 1;
	};

	// Writes `text` to `path`, its folders made.
	void WriteFile(const std::filesystem::path& path, const std::string& text);
	// The file: URI of `path`.
	std::string FileUri(const std::filesystem::path& path);
} // namespace Testing::Lsp
