package main

import (
	"reflect"
	"testing"
)

func hiddenOf(bounds ...int64) *SpanSet {
	s := &SpanSet{}
	for i := 0; i+1 < len(bounds); i += 2 {
		s.Add(bounds[i], bounds[i+1])
	}
	return s
}

func hiddenIs(t *testing.T, s *SpanSet, what string, bounds ...int64) {
	t.Helper()
	want := []Span{}
	for i := 0; i+1 < len(bounds); i += 2 {
		want = append(want, Span{bounds[i], bounds[i+1]})
	}
	got := append([]Span{}, s.All()...)
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("%s: got %v, want %v", what, got, want)
	}
}

func TestHiddenEmpty(t *testing.T) {
	var s SpanSet
	if s.Count() != 0 || s.Covered() != 0 || s.Contains(0) || len(s.All()) != 0 {
		t.Fatal("zero value is not empty")
	}
}

func TestHiddenAddIgnoresEmpty(t *testing.T) {
	var s SpanSet
	s.Add(5, 5)
	s.Add(9, 3)
	if s.Count() != 0 {
		t.Fatal("added an empty range")
	}
}

func TestHiddenAddMerges(t *testing.T) {
	s := hiddenOf(10, 20, 30, 40, 50, 60)
	hiddenIs(t, s, "three apart", 10, 20, 30, 40, 50, 60)
	s.Add(20, 30)
	hiddenIs(t, s, "touching both sides", 10, 40, 50, 60)
	s.Add(5, 55)
	hiddenIs(t, s, "covering", 5, 60)
	if s.Covered() != 55 {
		t.Fatal("covered")
	}
}

func TestHiddenAddInside(t *testing.T) {
	s := hiddenOf(0, 100)
	s.Add(10, 20)
	hiddenIs(t, s, "unchanged", 0, 100)
}

func TestHiddenRemoveEdges(t *testing.T) {
	s := hiddenOf(0, 10, 20, 30)
	s.Remove(5, 25)
	hiddenIs(t, s, "trimmed both", 0, 5, 25, 30)
	s.Remove(0, 5)
	hiddenIs(t, s, "whole span", 25, 30)
	s.Remove(30, 40)
	hiddenIs(t, s, "touching, nothing removed", 25, 30)
	s.Remove(26, 27)
	hiddenIs(t, s, "a hole", 25, 26, 27, 30)
	s.Remove(10, 9)
	if s.Covered() != 4 {
		t.Fatal("reversed range removed something")
	}
}

func TestHiddenContainsBounds(t *testing.T) {
	s := hiddenOf(-5, 0, 7, 8)
	if !s.Contains(-5) || !s.Contains(-1) || s.Contains(0) || !s.Contains(7) || s.Contains(8) || s.Contains(6) {
		t.Fatal("bounds")
	}
}

func TestHiddenMany(t *testing.T) {
	var s SpanSet
	for i := int64(0); i < 100000; i++ {
		s.Add(i*10, i*10+5)
	}
	if s.Count() != 100000 || s.Covered() != 500000 {
		t.Fatal("many adds")
	}
	for i := int64(0); i < 100000; i += 2 {
		s.Remove(i*10+2, i*10+3)
	}
	if s.Count() != 150000 || !s.Contains(999993) || s.Contains(999995) {
		t.Fatal("many removes")
	}
}
