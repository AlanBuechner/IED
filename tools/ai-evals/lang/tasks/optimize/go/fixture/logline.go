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

// LogLine is one access log line: "<timestamp> <ip> <method> <path> <status> <latency_ms> <bytes>".
type LogLine struct {
	Timestamp string
	IP        string
	Method    string
	Path      string
	Status    int64
	LatencyMs int64
	Bytes     int64
}

// Split returns the pieces of text between separators.
func Split(text string, separator byte) []string {
	var parts []string
	current := ""
	for i := 0; i < len(text); i++ {
		if text[i] == separator {
			parts = append(parts, current)
			current = ""
			continue
		}
		current = current + text[i:i+1]
	}
	parts = append(parts, current)
	return parts
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
	fields := Split(text, ' ')
	if len(fields) != fieldCount {
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
