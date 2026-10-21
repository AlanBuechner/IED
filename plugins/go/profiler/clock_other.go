//go:build !amd64

package profiler

const tickedByCounter = false

func now() int64 { return preciseNs() }
