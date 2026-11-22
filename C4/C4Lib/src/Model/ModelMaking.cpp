#include "Model/ModelMaking.h"
#include <algorithm>
#include <utility>

namespace Scripting
{
	ModelMaking::Request::Request(const ModelMaking& making, const char* what) : m_Making(making)
	{
		m_Making.Begin(what);
	}

	ModelMaking::Request::~Request()
	{
		m_Making.End();
	}

	ModelMaking::Sharing::Sharing(const ModelMaking& making) : m_Making(making)
	{
		m_Making.m_Sharing.fetch_add(1, std::memory_order_acq_rel);
	}

	ModelMaking::Sharing::~Sharing()
	{
		m_Making.m_Sharing.fetch_sub(1, std::memory_order_acq_rel);
	}

	void ModelMaking::Begin(const char* what) const
	{
		m_Lock.lock();
		if (m_Depth++ == 0) m_Maker.store(std::this_thread::get_id(), std::memory_order_relaxed);
		if (what) ++m_Requests[what];
	}

	// The outermost request shows what it made before the lock goes: a lowering that finds it finds it whole.
	void ModelMaking::End() const
	{
		if (--m_Depth == 0)
		{
			for (IShownWhenMade* made : m_ToShow) made->Show();
			m_ToShow.clear();
			for (std::atomic<bool>* shown : m_MadeShown) shown->store(true, std::memory_order_release);
			m_MadeShown.clear();
			m_Maker.store(std::thread::id(), std::memory_order_relaxed);
		}
		m_Lock.unlock();
	}

	void ModelMaking::ShowWhenMade(IShownWhenMade& made) const
	{
		if (!Making())
		{
			made.Show();
			return;
		}
		if (std::find(m_ToShow.begin(), m_ToShow.end(), &made) == m_ToShow.end()) m_ToShow.push_back(&made);
	}

	void ModelMaking::Made(std::atomic<bool>& shown) const
	{
		if (Making()) m_MadeShown.push_back(&shown);
	}

	void ModelMaking::Violate(const char* what) const
	{
		const std::lock_guard<std::mutex> keeping(m_ViolationsLock);
		if (std::find(m_Violations.begin(), m_Violations.end(), what) == m_Violations.end()) m_Violations.push_back(what);
	}

	std::vector<std::string> ModelMaking::TakeViolations() const
	{
		const std::lock_guard<std::mutex> keeping(m_ViolationsLock);
		return std::exchange(m_Violations, {});
	}

	std::map<std::string, uint64> ModelMaking::Requests() const
	{
		const std::lock_guard<std::recursive_mutex> reading(m_Lock);
		return m_Requests;
	}
} // namespace Scripting
