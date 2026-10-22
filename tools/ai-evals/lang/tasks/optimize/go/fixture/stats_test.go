package main

import "testing"

func TestParsesALine(t *testing.T) {
	line, ok := ParseLine("2024-05-01T00:00:00Z 10.0.0.1 GET /api/items/1 200 35 5120")
	if !ok {
		t.Fatal("does not parse")
	}
	if line.IP != "10.0.0.1" || line.Path != "/api/items/1" || line.Status != 200 || line.LatencyMs != 35 || line.Bytes != 5120 {
		t.Errorf("fields: %+v", line)
	}
	if _, ok := ParseLine("2024-05-01T00:00:00Z 10.0.0.1 GET /x 200 35"); ok {
		t.Error("six fields parsed")
	}
	if _, ok := ParseLine("2024-05-01T00:00:00Z 10.0.0.1 GET /x 700 35 1"); ok {
		t.Error("a bad status parsed")
	}
}

func TestNearestRank(t *testing.T) {
	values := []int64{5, 1, 4, 2, 3}
	cases := []struct{ percent, want int64 }{{50, 3}, {95, 5}, {1, 1}}
	for _, c := range cases {
		if got := Percentile(values, c.percent); got != c.want {
			t.Errorf("p%d: got %d, want %d", c.percent, got, c.want)
		}
	}
}

func TestReportsASmallLog(t *testing.T) {
	stats := Analyze("t 1.1.1.1 GET /a 200 10 100\nt 2.2.2.2 GET /b 503 50 10\nbroken\nt 1.1.1.1 GET /a 404 5 1\n")
	if stats.Requests != 3 || stats.Skipped != 1 || len(stats.IPs) != 2 {
		t.Errorf("requests %d, skipped %d, ips %d", stats.Requests, stats.Skipped, len(stats.IPs))
	}
}
