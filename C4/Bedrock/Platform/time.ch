// Time (D-63): the high-resolution counter, the wall clock and calendar dates. Measure durations with the prelude's
// clock_now_ns() (monotonic nanoseconds): the wall clock jumps when the system's time is set.
//     Platform::CalendarTime now = Platform::LocalCalendar(Platform::UnixTimeNs());
//     printf("%04d-%02d-%02d %02d:%02d\n", now.year, now.month, now.day, now.hour, now.minute);

namespace Platform
{
	const int64 kNanosecondsPerSecond = 1000000000;

	// The processor's high-resolution counter: monotonic, TickFrequency() ticks per second. The cheapest timestamp.
	[link("c4_time_ticks")] int64 Ticks();
	[link("c4_time_tick_frequency")] int64 TickFrequency();
	// The wall clock: nanoseconds since 1970-01-01 00:00:00 UTC without leap seconds; an int64 spans 1677 to 2262.
	[link("c4_time_unix_ns")] int64 UnixTimeNs();
	// Seconds to add to UTC for the local time at the instant `unixNs`, daylight saving time as it was then.
	[link("c4_time_local_offset_seconds")] int32 LocalOffsetSeconds(int64 unixNs);

	enum class Weekday { Sunday, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday }

	// A date and time of day in one time zone, in the Gregorian calendar extended to every year.
	struct CalendarTime
	{
		public int32 year;
		public int32 month;              // 1 to 12
		public int32 day;                // 1 to 31
		public int32 hour;               // 0 to 23
		public int32 minute;             // 0 to 59
		public int32 second;             // 0 to 59
		public int32 nanosecond;         // 0 to 999999999
		public Weekday weekday;
		public int32 utcOffsetSeconds;   // the zone's offset: local time minus UTC
	}

	// The calendar fields of the instant `unixNs` in UTC.
	CalendarTime UtcCalendar(int64 unixNs);
	// The same in the system's local time zone.
	CalendarTime LocalCalendar(int64 unixNs);
	// The same in a zone `utcOffsetSeconds` ahead of UTC.
	CalendarTime CalendarAtOffset(int64 unixNs, int32 utcOffsetSeconds);
	// The instant the fields name in their zone (utcOffsetSeconds), into `unixNs`; the weekday is not read. Fields out
	// of range carry over (month 13 is January of the next year, day 0 the last day of the month before). False when
	// the instant is outside what an int64 of nanoseconds holds.
	bool ToUnixNs(const CalendarTime& time, int64& unixNs);

	bool IsLeapYear(int32 year);
	// 28 to 31; 0 for a month outside 1 to 12.
	int32 DaysInMonth(int32 year, int32 month);
}
