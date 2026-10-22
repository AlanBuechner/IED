package main

import "testing"

func spent(category string, cents int64) Transaction {
	return Transaction{Date: "2024-05-01", Account: "card", Category: category, Cents: cents}
}

func TestRoundsHalfUp(t *testing.T) {
	if got := PercentTenths(1, 2000); got != 1 {
		t.Errorf("0.05%% is 0.1%%: got %d", got)
	}
	if got := PercentTenths(1, 8); got != 125 {
		t.Errorf("12.5%%: got %d", got)
	}
}

func TestOrdersByTotalThenName(t *testing.T) {
	rows := []Transaction{spent("b", -200), spent("a", -200), spent("c", -600), spent("pay", 1000)}
	want := "c 6.00 60.0%\na 2.00 20.0%\nb 2.00 20.0%\ntotal 10.00\n"
	if got := Report(rows); got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestNothingSpent(t *testing.T) {
	if got := Report(nil); got != "no spending\n" {
		t.Fatalf("got %q", got)
	}
}

func TestChecksMonths(t *testing.T) {
	for _, month := range []string{"2024-01", "1999-12"} {
		if !IsMonth(month) {
			t.Errorf("%q is a month", month)
		}
	}
	for _, month := range []string{"2024-13", "2024-00", "2024-1"} {
		if IsMonth(month) {
			t.Errorf("%q is not a month", month)
		}
	}
}
