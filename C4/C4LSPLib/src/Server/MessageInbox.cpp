#include "Server/MessageInbox.h"
#include <algorithm>

namespace C4LSP::Server
{
	namespace
	{
		const char* kCancelRequest = "$/cancelRequest";

		bool IsEditOf(const Json::Value& message, const std::string& uri)
		{
			const std::string method = message.Member("method").GetString();
			if (method != "textDocument/didChange" && method != "textDocument/didClose") return false;
			return std::string(message.Member("params").Member("textDocument").Member("uri").GetString()) == uri;
		}
	}

	std::string IdKey(const Json::Value& id)
	{
		if (id.IsString()) return "s:" + std::string(id.GetString());
		if (id.IsInt()) return "i:" + std::to_string(id.GetInt());
		return "";
	}

	void MessageInbox::Push(Json::Value message)
	{
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			if (std::string(message.Member("method").GetString()) == kCancelRequest)
			{
				const std::string key = IdKey(message.Member("params").Member("id"));
				if (!key.empty()) m_Cancelled.insert(key);
			}
			m_Waiting.push_back({ std::move(message), std::chrono::steady_clock::now() });
		}
		m_Arrived.notify_one();
		Wake();
	}

	void MessageInbox::Close()
	{
		{
			std::lock_guard<std::mutex> lock(m_Lock);
			m_Closed = true;
		}
		m_Arrived.notify_all();
		Wake();
	}

	void MessageInbox::Wake()
	{
		std::lock_guard<std::mutex> lock(m_WakeLock);
		if (m_Wake) m_Wake();
	}

	std::optional<MessageInbox::Arrived> MessageInbox::Next()
	{
		std::unique_lock<std::mutex> lock(m_Lock);
		m_Arrived.wait(lock, [this] { return !m_Waiting.empty() || m_Closed; });
		if (m_Waiting.empty()) return std::nullopt;
		Arrived next = std::move(m_Waiting.front());
		m_Waiting.pop_front();
		return next;
	}

	bool MessageInbox::IsCancelled(const Json::Value& id) const
	{
		const std::string key = IdKey(id);
		std::lock_guard<std::mutex> lock(m_Lock);
		return !key.empty() && m_Cancelled.count(key) > 0;
	}

	void MessageInbox::Forget(const Json::Value& id)
	{
		const std::string key = IdKey(id);
		std::lock_guard<std::mutex> lock(m_Lock);
		m_Cancelled.erase(key);
	}

	bool MessageInbox::HasEditOf(const std::string& uri) const
	{
		std::lock_guard<std::mutex> lock(m_Lock);
		return std::any_of(m_Waiting.begin(), m_Waiting.end(), [&uri](const Arrived& waiting) { return IsEditOf(waiting.message, uri); });
	}

	void MessageInbox::OnArrival(std::function<void()> wake)
	{
		std::lock_guard<std::mutex> lock(m_WakeLock);
		m_Wake = std::move(wake);
	}
} // namespace C4LSP::Server
