#include "Flag.h"

namespace Threading
{

	Flag::Flag() :
		m_Flag{ false }
	{}

	void Flag::SetFlag(bool flag)
	{
		std::lock_guard g(m_Mutex);
		m_Flag = flag;
		m_CondVar.notify_all();
	}

	void Flag::Wait(bool waitVal)
	{
		std::unique_lock lock(m_Mutex);
		m_CondVar.wait(lock, [this, waitVal]() {
			return m_Flag == waitVal;
		});
	}

}
