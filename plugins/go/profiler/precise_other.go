//go:build !windows

package profiler

import "time"

var epoch = time.Now()

// preciseNs is the monotonic clock in nanoseconds.
func preciseNs() int64 { return int64(time.Since(epoch)) }
