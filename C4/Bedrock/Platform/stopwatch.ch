// Platform::Stopwatch: elapsed time on the processor's high-resolution counter, for timing work and per-frame deltas.
// It starts when it is made; a zero Stopwatch counts from the counter's own zero.
//     Platform::Stopwatch watch;
//     DoWork();
//     printf("%.2f ms\n", watch.Milliseconds());
//     double dt = frames.Lap();   // seconds since the last lap: a frame's delta time

include "time.ch"

namespace Platform
{
	struct Stopwatch
	{
		[operator("create")] public void Init() { Restart(); }

		public void Restart() { start = Ticks(); }
		public int64 ElapsedTicks() const { return Ticks() - start; }
		public double Seconds() const { return (double)ElapsedTicks() / (double)TickFrequency(); }
		public double Milliseconds() const { return Seconds() * 1000.0; }
		// Exact for any span an int64 of nanoseconds holds.
		public int64 Nanoseconds() const;
		// The seconds since the last lap (or the start), and the next lap starts now.
		public double Lap();

		private int64 start;   // the counter when it started
	}
}
