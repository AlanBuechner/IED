#include "ChangeNotification.h"
#include <algorithm>

namespace ChangeNotification
{
	// ------------------------------------------------------------------------
	// Glob-style path matching
	// ------------------------------------------------------------------------
	bool MatchGlob(const std::string& path, const std::string& pattern)
	{
		return detail::MatchGlobRecursive(path.c_str(), pattern.c_str());
	}

	namespace detail
	{
		bool MatchGlobRecursive(const char* str, const char* pattern)
		{
			if (*pattern == '\0')
				return *str == '\0';

			if (*pattern == '*')
			{
				// '*' matches zero or more of any character
				while (*str != '\0')
				{
					if (MatchGlobRecursive(str + 1, pattern + 1))
						return true;
					++str;
				}
				return MatchGlobRecursive(str, pattern + 1);
			}

			if (*pattern == '?')
			{
				// '?' matches exactly one character
				return *str != '\0' && MatchGlobRecursive(str + 1, pattern + 1);
			}

			// Escape '/' in patterns so wildcards don't cross path segments
			// unless the pattern explicitly includes '/'
			if (*pattern == *str)
				return MatchGlobRecursive(str + 1, pattern + 1);

			return false;
		}
	}

	// ------------------------------------------------------------------------
	// ChangeQueue implementation
	// ------------------------------------------------------------------------
	ChangeQueue& ChangeQueue::GetInstance()
	{
		static ChangeQueue instance;
		return instance;
	}

	uint32 ChangeQueue::Subscribe(ChangeCallback callback, const std::string& pathFilter)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		Subscription sub;
		sub.id = m_NextID++;
		sub.callback = std::move(callback);
		sub.pathFilter = pathFilter;
		m_Subscriptions.push_back(std::move(sub));
		return sub.id;
	}

	void ChangeQueue::Unsubscribe(uint32 id)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Subscriptions.erase(
			std::remove_if(m_Subscriptions.begin(), m_Subscriptions.end(),
				[id](const Subscription& s) { return s.id == id; }),
			m_Subscriptions.end());
	}

	bool ChangeQueue::MatchesFilter(const std::string& filter, const std::string& objectPath)
	{
		if (filter.empty())
			return true; // Empty filter matches everything
		return MatchGlob(objectPath, filter);
	}

	void ChangeQueue::InvokeMatching(const ChangeRecord& record)
	{
		// Invoke callbacks outside the lock to avoid potential deadlocks
		std::vector<std::pair<ChangeCallback, bool>> matched;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			matched.reserve(m_Subscriptions.size());
			for (const auto& sub : m_Subscriptions)
			{
				bool matches = MatchesFilter(sub.pathFilter, record.ObjectPath);
				matched.emplace_back(sub.callback, matches);
			}
		}

		for (auto& [callback, matches] : matched)
		{
			if (matches && callback)
			{
				callback(record);
			}
		}
	}

	void ChangeQueue::Notify(const ChangeRecord& record)
	{
		if (m_BatchMode.load(std::memory_order_acquire))
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_PendingRecords.push_back(record);
		}
		else
		{
			InvokeMatching(record);
		}
	}

	void ChangeQueue::SetBatchMode(bool enabled)
	{
		m_BatchMode.store(enabled, std::memory_order_release);
	}

	void ChangeQueue::ProcessPendingNotifications()
	{
		std::vector<ChangeRecord> records;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			if (m_PendingRecords.empty())
				return;
			records.swap(m_PendingRecords);
		}

		for (const auto& record : records)
		{
			InvokeMatching(record);
		}
	}
}
