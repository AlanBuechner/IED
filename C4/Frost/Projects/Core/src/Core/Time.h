#pragma once

namespace Core
{
	class Time
	{
	public:
		Time() {}

		static inline double GetDeltaTime() { return s_DeltaTime; }
		static inline float GetDeltaTimeF() { return s_DeltaTime; }
		static inline double GetDeltaSeconds() { return s_DeltaTime; }
		static inline float GetDeltaSecondsF() { return s_DeltaTime; }
		static inline double GetDeltaMilliseconds() { return s_DeltaTime * 1000.0f; }
		static inline float GetDeltaMillisecondsF() { return s_DeltaTime * 1000.0f; }

		static inline float GetFPS() { return (float)(1.0 / s_DeltaTime); }

		static double GetTime();

		static void UpdateDeltaTime();

	protected:
		static double s_DeltaTime;
		static double s_LastFrameTime;
	};
}
