package main

import (
	"bufio"
	"cmp"
	"fmt"
	"io"
	"math"
	"slices"
	"strings"
)

const (
	topPaths    = 10
	topErrorIPs = 5
	serverError = 500
	clientError = 400
	redirect    = 300
	success     = 200
	readSize    = 64 << 10 // bytes read from the log at a time
)

// Stats is what a log adds up to; NewStats makes an empty one.
type Stats struct {
	Requests     int64
	Skipped      int64
	Bytes        int64
	IPs          map[string]struct{} // each address seen
	Paths        map[string]int64    // requests by path
	Latencies2xx []int64
	Latencies4xx []int64
	Latencies5xx []int64
	ServerErrors map[string]int64 // 5xx responses by address
}

// NewStats is an empty Stats, ready to add lines to.
func NewStats() Stats {
	return Stats{IPs: make(map[string]struct{}), Paths: make(map[string]int64), ServerErrors: make(map[string]int64)}
}

// AddLine counts one request.
func (s *Stats) AddLine(line LogLine) {
	s.Requests++
	s.Bytes += line.Bytes
	s.IPs[line.IP] = struct{}{}
	s.Paths[line.Path]++
	if line.Status >= success && line.Status < redirect {
		s.Latencies2xx = append(s.Latencies2xx, line.LatencyMs)
	}
	if line.Status >= clientError && line.Status < serverError {
		s.Latencies4xx = append(s.Latencies4xx, line.LatencyMs)
	}
	if line.Status >= serverError {
		s.Latencies5xx = append(s.Latencies5xx, line.LatencyMs)
		s.ServerErrors[line.IP]++
	}
}

// addText counts one line of the log, given without its line break; an empty one is not a line.
func (s *Stats) addText(text string) {
	if text == "" {
		return
	}
	if line, ok := ParseLine(text); ok {
		s.AddLine(line)
	} else {
		s.Skipped++
	}
}

// AnalyzeReader adds up every line r holds, reading it a piece at a time; lines that do not parse are counted as
// skipped.
func AnalyzeReader(r io.Reader) (Stats, error) {
	stats := NewStats()
	lines := bufio.NewScanner(r) // each line without its "\n" and one "\r" before it
	lines.Buffer(make([]byte, 0, readSize), math.MaxInt)
	for lines.Scan() {
		stats.addText(lines.Text())
	}
	return stats, lines.Err()
}

// Analyze adds up every line of a log; lines that do not parse are counted as skipped.
func Analyze(log string) Stats {
	stats, _ := AnalyzeReader(strings.NewReader(log)) // reading a string does not fail
	return stats
}

// PercentileOfSorted is the nearest-rank percentile of values in ascending order: the value at rank
// ceil(percent * count / 100), counting from 1.
func PercentileOfSorted(sorted []int64, percent int64) int64 {
	rank := max((percent*int64(len(sorted))+99)/100, 1)
	return sorted[rank-1]
}

// Percentile is the nearest-rank percentile of values in any order.
func Percentile(values []int64, percent int64) int64 {
	sorted := slices.Clone(values)
	slices.Sort(sorted)
	return PercentileOfSorted(sorted, percent)
}

func writeLatency(out *strings.Builder, name string, values []int64) {
	out.WriteString("latency_" + name)
	if len(values) == 0 {
		out.WriteString(" none\n")
		return
	}
	sorted := slices.Clone(values)
	slices.Sort(sorted)
	fmt.Fprintf(out, " %d %d %d\n", PercentileOfSorted(sorted, 50), PercentileOfSorted(sorted, 95), PercentileOfSorted(sorted, 99))
}

type nameCount struct {
	name  string
	count int64
}

// byCountThenName orders the largest counts first, ties by name.
func byCountThenName(a, b nameCount) int {
	return cmp.Or(cmp.Compare(b.count, a.count), strings.Compare(a.name, b.name))
}

// writeTop writes the `most` largest counts, a "name count" line each.
func writeTop(out *strings.Builder, counts map[string]int64, most int) {
	ranked := make([]nameCount, 0, len(counts))
	for name, count := range counts {
		ranked = append(ranked, nameCount{name, count})
	}
	slices.SortFunc(ranked, byCountThenName)
	for _, entry := range ranked[:min(most, len(ranked))] {
		fmt.Fprintf(out, "%s %d\n", entry.name, entry.count)
	}
}

// Report is the summary logstats prints.
func Report(stats Stats) string {
	var out strings.Builder
	fmt.Fprintf(&out, "requests %d\nskipped %d\nunique_ips %d\nbytes %d\ntop_paths\n", stats.Requests, stats.Skipped, len(stats.IPs), stats.Bytes)
	writeTop(&out, stats.Paths, topPaths)
	writeLatency(&out, "2xx", stats.Latencies2xx)
	writeLatency(&out, "4xx", stats.Latencies4xx)
	writeLatency(&out, "5xx", stats.Latencies5xx)
	out.WriteString("top_error_ips\n")
	writeTop(&out, stats.ServerErrors, topErrorIPs)
	return out.String()
}
