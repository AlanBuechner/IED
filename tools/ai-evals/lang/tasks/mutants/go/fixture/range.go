package main

import (
	"fmt"
	"strings"
)

// Operator is how a version must compare with a comparator's version.
type Operator int

const (
	Equal Operator = iota
	Greater
	GreaterOrEqual
	Less
	LessOrEqual
)

// Comparator is one condition of a range.
type Comparator struct {
	Op      Operator
	Version Version
}

// Range is comparators that must all hold; ^V and ~V each stand for two, a lower and an upper bound.
type Range []Comparator

// caretLimit is the first version ^v excludes: the next major, or for 0.y.z the next minor, or for 0.0.z the next
// patch.
func caretLimit(v Version) Version {
	switch {
	case v.Major > 0:
		return Version{Major: v.Major + 1}
	case v.Minor > 0:
		return Version{Minor: v.Minor + 1}
	default:
		return Version{Patch: v.Patch + 1}
	}
}

// tildeLimit is the first version ~v excludes: the next minor.
func tildeLimit(v Version) Version {
	return Version{Major: v.Major, Minor: v.Minor + 1}
}

// cutOperator takes the operator off the front of a comparator, the longer ones first so that ">=" is not read as ">".
func cutOperator(field string) (string, string) {
	for _, op := range []string{">=", "<=", ">", "<", "=", "^", "~"} {
		if rest, ok := strings.CutPrefix(field, op); ok {
			return op, rest
		}
	}
	return "=", field
}

func operatorOf(op string) Operator {
	switch op {
	case ">":
		return Greater
	case ">=":
		return GreaterOrEqual
	case "<":
		return Less
	case "<=":
		return LessOrEqual
	}
	return Equal
}

func parseComparator(field string) ([]Comparator, bool) {
	op, text := cutOperator(field)
	v, err := ParseVersion(text)
	if err != nil {
		return nil, false
	}
	switch op {
	case "^":
		return []Comparator{{GreaterOrEqual, v}, {Less, caretLimit(v)}}, true
	case "~":
		return []Comparator{{GreaterOrEqual, v}, {Less, tildeLimit(v)}}, true
	}
	return []Comparator{{operatorOf(op), v}}, true
}

func invalidRange(text string) error {
	return fmt.Errorf("invalid range %s", text)
}

// ParseRange reads comparators separated by spaces: =V, >V, >=V, <V, <=V, ^V, ~V, or a bare V meaning =V.
// ^1.2.3 is >=1.2.3 <2.0.0, ^0.2.3 is >=0.2.3 <0.3.0, ^0.0.3 is >=0.0.3 <0.0.4; ~1.2.3 is >=1.2.3 <1.3.0.
// An empty range is an error.
func ParseRange(text string) (Range, error) {
	var r Range
	for _, field := range strings.Fields(text) {
		comparators, ok := parseComparator(field)
		if !ok {
			return nil, invalidRange(text)
		}
		r = append(r, comparators...)
	}
	if len(r) == 0 {
		return nil, invalidRange(text)
	}
	return r, nil
}

func (c Comparator) holds(v Version) bool {
	order := Compare(v, c.Version)
	switch c.Op {
	case Greater:
		return order > 0
	case GreaterOrEqual:
		return order >= 0
	case Less:
		return order < 0
	case LessOrEqual:
		return order <= 0
	}
	return order == 0
}

func sameCore(a, b Version) bool {
	return a.Major == b.Major && a.Minor == b.Minor && a.Patch == b.Patch
}

func (r Range) allowsPrerelease(v Version) bool {
	for _, c := range r {
		if len(c.Version.Prerelease) > 0 && sameCore(c.Version, v) {
			return true
		}
	}
	return false
}

// Satisfies reports whether v meets every comparator of r. A version with a pre-release satisfies r only when a
// comparator's version has the same major, minor and patch and a pre-release too (npm's rule).
func (r Range) Satisfies(v Version) bool {
	for _, c := range r {
		if !c.holds(v) {
			return false
		}
	}
	return len(v.Prerelease) == 0 || r.allowsPrerelease(v)
}
