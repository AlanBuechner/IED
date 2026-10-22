package main

import (
	"strconv"
	"strings"
)

const (
	fieldCount    = 7
	lowestStatus  = 100
	highestStatus = 599
)

// LogLine is one access log line: "<timestamp> <ip> <method> <path> <status> <latency_ms> <bytes>"; its text fields
// are substrings of the line.
type LogLine struct {
	Timestamp string
	IP        string
	Method    string
	Path      string
	Status    int64
	LatencyMs int64
	Bytes     int64
}

// splitFields cuts text at each space into exactly len(fields) pieces; false when it has more or fewer.
func splitFields(text string, fields []string) bool {
	last := len(fields) - 1
	for i := range last {
		field, rest, found := strings.Cut(text, " ")
		if !found {
			return false
		}
		fields[i], text = field, rest
	}
	fields[last] = text
	return strings.IndexByte(text, ' ') < 0
}

// parseNumber reads decimal text, an optional '-' and at least one digit, as a number; false for anything else.
func parseNumber(text string) (int64, bool) {
	if strings.HasPrefix(text, "+") {
		return 0, false
	}
	value, err := strconv.ParseInt(text, 10, 64)
	return value, err == nil
}

// ParseLine reads one log line; false when it does not have the seven fields, or its numbers do not read.
func ParseLine(text string) (LogLine, bool) {
	var fields [fieldCount]string
	if !splitFields(text, fields[:]) {
		return LogLine{}, false
	}
	status, statusOk := parseNumber(fields[4])
	latency, latencyOk := parseNumber(fields[5])
	size, sizeOk := parseNumber(fields[6])
	if !statusOk || !latencyOk || !sizeOk {
		return LogLine{}, false
	}
	if status < lowestStatus || status > highestStatus || latency < 0 || size < 0 {
		return LogLine{}, false
	}
	return LogLine{Timestamp: fields[0], IP: fields[1], Method: fields[2], Path: fields[3], Status: status, LatencyMs: latency, Bytes: size}, true
}
