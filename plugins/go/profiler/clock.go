package profiler

import "time"

const probeFor = 20 * time.Millisecond

// clock relates the client's ticks (now) to time: how many a second, and where the session began on the precise clock
// and on the wall clock. Ticks are the processor's time stamp counter where there is one (amd64), its rate measured at
// start (the Hello says it is estimated; Calibration pairs correct it), else nanoseconds of the precise clock.
type clock struct {
	frequency    int64
	estimated    bool
	startTicks   int64
	startPrecise int64
	startUnix    int64
}

func startClock() clock {
	c := clock{frequency: 1_000_000_000, estimated: tickedByCounter}
	if tickedByCounter {
		ticks0, precise0 := now(), preciseNs()
		time.Sleep(probeFor)
		ticks1, precise1 := now(), preciseNs()
		if precise1 > precise0 && ticks1 > ticks0 {
			c.frequency = int64(float64(ticks1-ticks0) * 1e9 / float64(precise1-precise0))
		}
	}
	c.startTicks, c.startPrecise, c.startUnix = now(), preciseNs(), time.Now().UnixNano()
	return c
}

// unixNs is the wall-clock time now, as fine as the precise clock: the session's start plus the time since.
func (c clock) unixNs() int64 {
	return c.startUnix + (preciseNs() - c.startPrecise)
}

// ticksAt is a wall-clock time (a runtime statistic's) on the client's clock.
func (c clock) ticksAt(t time.Time) int64 {
	return now() - int64(float64(time.Now().UnixNano()-t.UnixNano())*float64(c.frequency)/1e9)
}
