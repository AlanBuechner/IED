package main

import (
	"errors"
	"fmt"
	"io"
	"slices"
	"strings"
	"testing"
	"testing/iotest"
)

func TestFieldsAreExactlySeven(t *testing.T) {
	if line, ok := ParseLine("t  GET /a 200 10 100"); !ok || line.IP != "" || line.Method != "GET" {
		t.Errorf("an empty field: %+v, %v", line, ok)
	}
	for _, text := range []string{"t 1.1.1.1 GET /a 200 10 100 more", "t 1.1.1.1 GET /a 200 10 100 ", "t 1.1.1.1  GET /a 200 10 100", ""} {
		if _, ok := ParseLine(text); ok {
			t.Errorf("%q parsed", text)
		}
	}
}

func TestNumbersAreDecimal(t *testing.T) {
	if line, ok := ParseLine("t 1.1.1.1 GET /a 599 -0 9223372036854775807"); !ok || line.LatencyMs != 0 || line.Bytes != 9223372036854775807 {
		t.Errorf("-0 and the largest: %+v, %v", line, ok)
	}
	for _, numbers := range []string{"+200 10 100", "200 10 9223372036854775808", "200 1_0 100", "200 0x10 100", "200 -1 100", "99 10 100"} {
		if _, ok := ParseLine("t 1.1.1.1 GET /a " + numbers); ok {
			t.Errorf("%q parsed", numbers)
		}
	}
}

func TestLineEndings(t *testing.T) {
	stats := Analyze("t 1.1.1.1 GET /a 200 10 100\r\n\r\n\nt 2.2.2.2 GET /a 200 10 100\r\r\nt 3.3.3.3 GET /b 404 5 1")
	if stats.Requests != 2 || stats.Skipped != 1 {
		t.Errorf("requests %d, skipped %d", stats.Requests, stats.Skipped)
	}
}

func TestReadsLongLines(t *testing.T) {
	log := "t 1.1.1.1 GET /" + strings.Repeat("a", 3*readSize) + " 200 10 100\n" + strings.Repeat("x", 2*readSize) + "\nt 2.2.2.2 GET /b 200 10 100\n"
	stats := Analyze(log)
	if stats.Requests != 2 || stats.Skipped != 1 {
		t.Errorf("requests %d, skipped %d", stats.Requests, stats.Skipped)
	}
}

func TestReportsReadErrors(t *testing.T) {
	failure := errors.New("the disk is gone")
	log := io.MultiReader(strings.NewReader("t 1.1.1.1 GET /a 200 10 100\n"), iotest.ErrReader(failure))
	if _, err := AnalyzeReader(log); !errors.Is(err, failure) {
		t.Errorf("got %v", err)
	}
}

func TestPercentileOfSorted(t *testing.T) {
	sorted := []int64{1, 2, 3, 4}
	cases := []struct{ percent, want int64 }{{0, 1}, {50, 2}, {75, 3}, {76, 4}, {100, 4}}
	for _, c := range cases {
		if got := PercentileOfSorted(sorted, c.percent); got != c.want {
			t.Errorf("p%d: got %d, want %d", c.percent, got, c.want)
		}
	}
}

func TestReportRanksAndCuts(t *testing.T) {
	var log strings.Builder
	for i := range 12 {
		fmt.Fprintf(&log, "t 10.1.0.%d GET /p%02d 500 %d 1\n", i, i, i)
	}
	log.WriteString("t 10.1.0.11 GET /p11 500 1 1\nt 10.1.0.11 GET /p11 404 1 1\n")
	want := "requests 14\nskipped 0\nunique_ips 12\nbytes 14\ntop_paths\n/p11 3\n/p00 1\n/p01 1\n/p02 1\n/p03 1\n/p04 1\n/p05 1\n/p06 1\n/p07 1\n/p08 1\n" +
		"latency_2xx none\nlatency_4xx 1 1 1\nlatency_5xx 5 11 11\n" +
		"top_error_ips\n10.1.0.11 2\n10.1.0.0 1\n10.1.0.1 1\n10.1.0.10 1\n10.1.0.2 1\n"
	if got := Report(Analyze(log.String())); got != want {
		t.Errorf("got\n%s\nwant\n%s", got, want)
	}
}

func TestReportLeavesTheLatenciesAlone(t *testing.T) {
	stats := Analyze("t a GET /a 200 30 1\nt a GET /a 200 10 1\nt a GET /a 200 20 1\n")
	Report(stats)
	if !slices.Equal(stats.Latencies2xx, []int64{30, 10, 20}) {
		t.Errorf("got %v", stats.Latencies2xx)
	}
}
