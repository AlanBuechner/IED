package main

import (
	"cmp"
	"fmt"
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
)

// PathCount is how many requests asked for a path.
type PathCount struct {
	Path  string
	Count int64
}

// IPCount is how many requests came from an address.
type IPCount struct {
	IP    string
	Count int64
}

// Stats is what a log adds up to.
type Stats struct {
	Requests     int64
	Skipped      int64
	Bytes        int64
	IPs          []string // each address once
	Paths        []PathCount
	Latencies2xx []int64
	Latencies4xx []int64
	Latencies5xx []int64
	ServerErrors []IPCount // 5xx responses by address
}

func (s *Stats) countIP(ip string) {
	for _, seen := range s.IPs {
		if seen == ip {
			return
		}
	}
	s.IPs = append(s.IPs, ip)
}

func (s *Stats) countPath(path string) {
	for i := range s.Paths {
		if s.Paths[i].Path == path {
			s.Paths[i].Count++
			return
		}
	}
	s.Paths = append(s.Paths, PathCount{path, 1})
}

func (s *Stats) countServerError(ip string) {
	for i := range s.ServerErrors {
		if s.ServerErrors[i].IP == ip {
			s.ServerErrors[i].Count++
			return
		}
	}
	s.ServerErrors = append(s.ServerErrors, IPCount{ip, 1})
}

// AddLine counts one request.
func (s *Stats) AddLine(line LogLine) {
	s.Requests++
	s.Bytes += line.Bytes
	s.countIP(line.IP)
	s.countPath(line.Path)
	if line.Status >= success && line.Status < redirect {
		s.Latencies2xx = append(s.Latencies2xx, line.LatencyMs)
	}
	if line.Status >= clientError && line.Status < serverError {
		s.Latencies4xx = append(s.Latencies4xx, line.LatencyMs)
	}
	if line.Status >= serverError {
		s.Latencies5xx = append(s.Latencies5xx, line.LatencyMs)
		s.countServerError(line.IP)
	}
}

// Analyze adds up every line of a log; lines that do not parse are counted as skipped.
func Analyze(log string) Stats {
	var stats Stats
	for _, text := range Split(log, '\n') {
		row := strings.TrimSuffix(text, "\r")
		if row == "" {
			continue
		}
		if line, ok := ParseLine(row); ok {
			stats.AddLine(line)
		} else {
			stats.Skipped++
		}
	}
	return stats
}

// Percentile is the nearest-rank percentile: the value at rank ceil(percent * count / 100), counting from 1, of the
// sorted values.
func Percentile(values []int64, percent int64) int64 {
	sorted := slices.Clone(values)
	slices.Sort(sorted)
	rank := (percent*int64(len(sorted)) + 99) / 100
	if rank < 1 {
		rank = 1
	}
	return sorted[rank-1]
}

func writeLatency(out *strings.Builder, name string, values []int64) {
	out.WriteString("latency_" + name)
	if len(values) == 0 {
		out.WriteString(" none\n")
		return
	}
	fmt.Fprintf(out, " %d %d %d\n", Percentile(values, 50), Percentile(values, 95), Percentile(values, 99))
}

// byPathCount orders the most requested paths first, ties by path.
func byPathCount(a, b PathCount) int {
	return cmp.Or(cmp.Compare(b.Count, a.Count), strings.Compare(a.Path, b.Path))
}

// byIPCount orders the addresses with the most server errors first, ties by address.
func byIPCount(a, b IPCount) int {
	return cmp.Or(cmp.Compare(b.Count, a.Count), strings.Compare(a.IP, b.IP))
}

// Report is the summary logstats prints.
func Report(stats Stats) string {
	var out strings.Builder
	fmt.Fprintf(&out, "requests %d\nskipped %d\nunique_ips %d\nbytes %d\ntop_paths\n", stats.Requests, stats.Skipped, len(stats.IPs), stats.Bytes)
	paths := slices.Clone(stats.Paths)
	slices.SortFunc(paths, byPathCount)
	for i := 0; i < len(paths) && i < topPaths; i++ {
		fmt.Fprintf(&out, "%s %d\n", paths[i].Path, paths[i].Count)
	}
	writeLatency(&out, "2xx", stats.Latencies2xx)
	writeLatency(&out, "4xx", stats.Latencies4xx)
	writeLatency(&out, "5xx", stats.Latencies5xx)
	out.WriteString("top_error_ips\n")
	errorIPs := slices.Clone(stats.ServerErrors)
	slices.SortFunc(errorIPs, byIPCount)
	for i := 0; i < len(errorIPs) && i < topErrorIPs; i++ {
		fmt.Fprintf(&out, "%s %d\n", errorIPs[i].IP, errorIPs[i].Count)
	}
	return out.String()
}
