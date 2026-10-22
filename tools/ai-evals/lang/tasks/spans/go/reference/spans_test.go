package main

import "testing"

func TestAddMergesTouching(t *testing.T) {
	var s SpanSet
	s.Add(1, 3)
	s.Add(3, 5)
	if s.Count() != 1 || s.Covered() != 4 {
		t.Fatalf("got %v", s.All())
	}
}

func TestRemoveSplits(t *testing.T) {
	var s SpanSet
	s.Add(0, 10)
	s.Remove(4, 6)
	if s.Count() != 2 || !s.Contains(3) || s.Contains(4) || s.Contains(5) || !s.Contains(6) {
		t.Fatalf("got %v", s.All())
	}
}
