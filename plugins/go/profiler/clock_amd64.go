package profiler

const tickedByCounter = true

// now is the processor's time stamp counter (clock_amd64.s): a few nanoseconds to read.
func now() int64
