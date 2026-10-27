// The work week (docs/04 "Pacing", ai.claude.paceDays): which time a weekly window's pace counts - all of it, or only the
// work days', each from midnight to midnight in the local time zone of the computer the server runs on (the owner's own),
// daylight saving time as it is at each instant. A weekly window resets at its own time (a Wednesday 14:00), so its days
// off fall anywhere inside it; while one lasts the counted time stands still.

include "bedrock.ch"

namespace Ai
{
	const int32 kWorkWeekDays = 0x3E;   // Monday to Friday: a bit per Platform::Weekday, Sunday the lowest

	struct PaceDays
	{
		public bool workWeek;                 // only the work days count
		public int32 workDays;                // a bit per Platform::Weekday, Sunday the lowest
		public func<int32(int64)> offsetOf;   // the seconds local time is ahead of UTC at a Unix ms time; read only for a work week
	}

	// A stretch of days off inside a window: its start and end (Unix ms) and where an even pace over the counted time stands
	// through it, 0 (the window's start) to 1 (its reset).
	struct DaysOff
	{
		public int64 startMs;
		public int64 endMs;
		public double at;
	}

	// Every day counts (ai.claude.paceDays "week", the default).
	PaceDays EveryDay();
	// Only `workDays` count, in the system's local time zone.
	PaceDays WorkDaysOnly(int32 workDays);
	// ai.claude.workDays' value (["mon", "tue", ...]) as a bit per weekday; kWorkWeekDays when it is not a list.
	int32 ReadWorkDays(const Serialization::DomNode& days);
	// The time `days` counts in [fromMs, toMs): all of it, or the part on work days.
	int64 CountedMs(const PaceDays& days, int64 fromMs, int64 toMs);
	// The stretches of [fromMs, toMs) on days off, each with where the even line over the counted time of the whole range
	// stands through it; none when every day counts.
	Core::Array<DaysOff> DaysOffIn(const PaceDays& days, int64 fromMs, int64 toMs);
}
