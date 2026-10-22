package main

import (
	"slices"
	"sort"
)

// Span is the half-open range [Start, End).
type Span struct {
	Start int64 // inclusive
	End   int64 // exclusive
}

// SpanSet is a set of integers as disjoint spans; its zero value is an empty set.
type SpanSet struct {
	spans   []Span
	covered int64
}

func (s *SpanSet) firstEndingFrom(point int64) int {
	return sort.Search(len(s.spans), func(i int) bool { return s.spans[i].End >= point })
}

func (s *SpanSet) firstStartingAfter(point int64) int {
	return sort.Search(len(s.spans), func(i int) bool { return s.spans[i].Start > point })
}

func (s *SpanSet) replace(first, last int, with []Span) {
	for _, span := range s.spans[first:last] {
		s.covered -= span.End - span.Start
	}
	for _, span := range with {
		s.covered += span.End - span.Start
	}
	s.spans = slices.Replace(s.spans, first, last, with...)
}

// Add adds every integer of [start, end); a range with end <= start does nothing.
func (s *SpanSet) Add(start, end int64) {
	if end <= start {
		return
	}
	first, last := s.firstEndingFrom(start), s.firstStartingAfter(end)
	merged := Span{start, end}
	if first < last {
		merged.Start = min(merged.Start, s.spans[first].Start)
		merged.End = max(merged.End, s.spans[last-1].End)
	}
	s.replace(first, last, []Span{merged})
}

// Remove removes every integer of [start, end); a range with end <= start does nothing.
func (s *SpanSet) Remove(start, end int64) {
	if end <= start {
		return
	}
	first, last := s.firstEndingFrom(start+1), s.firstStartingAfter(end-1)
	if first >= last {
		return
	}
	var kept []Span
	if s.spans[first].Start < start {
		kept = append(kept, Span{s.spans[first].Start, start})
	}
	if s.spans[last-1].End > end {
		kept = append(kept, Span{end, s.spans[last-1].End})
	}
	s.replace(first, last, kept)
}

// Contains reports whether point is in the set.
func (s *SpanSet) Contains(point int64) bool {
	i := s.firstEndingFrom(point + 1)
	return i < len(s.spans) && s.spans[i].Start <= point
}

// Count is how many disjoint spans the set is.
func (s *SpanSet) Count() int { return len(s.spans) }

// Covered is how many integers the set holds.
func (s *SpanSet) Covered() int64 { return s.covered }

// All returns the spans in ascending order.
func (s *SpanSet) All() []Span { return append([]Span(nil), s.spans...) }
