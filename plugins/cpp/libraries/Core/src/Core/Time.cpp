#include "Time.h"
#include <chrono>

double Core::Time::s_DeltaTime = 0.0f;
double Core::Time::s_LastFrameTime = Core::Time::GetTime();

namespace Core
{
	using namespace std::chrono;

	double Time::GetTime()
	{
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}

	void Time::UpdateDeltaTime()
	{
		double time = GetTime();
		s_DeltaTime = time - s_LastFrameTime;
		s_LastFrameTime = time;
	}
}
