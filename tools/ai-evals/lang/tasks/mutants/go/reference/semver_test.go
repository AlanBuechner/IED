package main

import (
	"slices"
	"testing"
)

func mustParse(t *testing.T, text string) Version {
	t.Helper()
	v, err := ParseVersion(text)
	if err != nil {
		t.Fatalf("ParseVersion(%q): %v", text, err)
	}
	return v
}

func compareTexts(t *testing.T, a, b string) int {
	t.Helper()
	return Compare(mustParse(t, a), mustParse(t, b))
}

func TestParsesTheCore(t *testing.T) {
	v := mustParse(t, "1.20.300")
	if v.Major != 1 || v.Minor != 20 || v.Patch != 300 || v.Prerelease != nil || v.Build != nil {
		t.Errorf("1.20.300: got %+v", v)
	}
	for _, text := range []string{"0.0.0", "10.20.30"} {
		mustParse(t, text)
	}
	if v := mustParse(t, "9223372036854775807.0.0"); v.Major != 9223372036854775807 {
		t.Errorf("the largest int64: got %d", v.Major)
	}
}

func TestParsesIdentifiers(t *testing.T) {
	v := mustParse(t, "1.0.0-alpha.1.x-y.0a+build.001.exp-2")
	if want := []string{"alpha", "1", "x-y", "0a"}; !slices.Equal(v.Prerelease, want) {
		t.Errorf("pre-release: got %q, want %q", v.Prerelease, want)
	}
	if want := []string{"build", "001", "exp-2"}; !slices.Equal(v.Build, want) {
		t.Errorf("build: got %q, want %q", v.Build, want)
	}
}

func TestParsesBuildWithoutPrerelease(t *testing.T) {
	v := mustParse(t, "1.0.0+build-1.2")
	if v.Prerelease != nil || !slices.Equal(v.Build, []string{"build-1", "2"}) {
		t.Errorf("a hyphen after + belongs to the build: got %+v", v)
	}
	mustParse(t, "1.0.0+20130313144700")
	mustParse(t, "1.0.0-beta+exp.sha.5114f85")
}

func TestAcceptsHyphensInIdentifiers(t *testing.T) {
	cases := []struct {
		text              string
		prerelease, build []string
	}{
		{"1.2.3-x-y", []string{"x-y"}, nil},
		{"1.2.3--", []string{"-"}, nil},
		{"1.2.3-alpha--1.-", []string{"alpha--1", "-"}, nil},
		{"1.2.3+-", nil, []string{"-"}},
	}
	for _, c := range cases {
		v := mustParse(t, c.text)
		if !slices.Equal(v.Prerelease, c.prerelease) || !slices.Equal(v.Build, c.build) {
			t.Errorf("%s: got %+v", c.text, v)
		}
	}
}

func TestRejectsLeadingZeros(t *testing.T) {
	for _, text := range []string{"01.2.3", "1.02.3", "1.2.03", "00.0.0", "1.2.3-01", "1.2.3-alpha.007", "1.2.3-00"} {
		if _, err := ParseVersion(text); err == nil {
			t.Errorf("%s parsed", text)
		}
	}
	for _, text := range []string{"1.2.3-0", "1.2.3-0a", "1.2.3-0-1", "1.2.3-00a", "1.2.3+001", "1.2.3+0.00", "1.2.3-rc.1+007"} {
		mustParse(t, text)
	}
}

func TestRejectsBadCores(t *testing.T) {
	bad := []string{"", "1", "1.2", "1.2.", ".1.2", "1..3", "1.2.3.4", "a.b.c", "-1.2.3", "v1.2.3", "1.2.x", "1.2.3.alpha",
		"99999999999999999999.0.0", "1.9223372036854775808.0"}
	for _, text := range bad {
		if _, err := ParseVersion(text); err == nil {
			t.Errorf("%q parsed", text)
		}
	}
}

func TestRejectsEmptyIdentifiers(t *testing.T) {
	for _, text := range []string{"1.2.3-", "1.2.3+", "1.2.3-a..b", "1.2.3-.a", "1.2.3-a.", "1.2.3+a..b", "1.2.3+.a", "1.2.3-+b", "1.2.3-a+"} {
		if _, err := ParseVersion(text); err == nil {
			t.Errorf("%q parsed", text)
		}
	}
}

func TestRejectsOtherCharacters(t *testing.T) {
	bad := []string{"1.2.3-a_b", "1.2.3-a^b", "1.2.3-a`b", "1.2.3-[a]", "1.2.3-a b", "1.2.3-a$", "1.2.3+a_b", "1.2.3+a+b",
		" 1.2.3", "1.2.3 ", "1.2.3-alpha!"}
	for _, text := range bad {
		if _, err := ParseVersion(text); err == nil {
			t.Errorf("%q parsed", text)
		}
	}
	mustParse(t, "1.2.3-AZaz09")
	mustParse(t, "1.2.3+AZaz09-")
}

func TestReportsTheInvalidText(t *testing.T) {
	if _, err := ParseVersion("1.2"); err == nil || err.Error() != "invalid version 1.2" {
		t.Errorf("got %v", err)
	}
}

func TestFormatsVersions(t *testing.T) {
	for _, text := range []string{"1.2.3", "0.0.0", "1.0.0-alpha.1", "1.0.0+build.5", "1.0.0-rc.1+build.5", "10.20.30-x-y.0a+001.exp-2"} {
		if got := mustParse(t, text).String(); got != text {
			t.Errorf("%s: got %s", text, got)
		}
	}
}

func TestComparesNumbersByValue(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.2.3", "1.2.4", -1},
		{"1.2.10", "1.2.9", 1},
		{"1.2.0", "1.4.0", -1},
		{"1.3.0", "1.2.9", 1},
		{"1.9.0", "1.10.0", -1},
		{"3.0.0", "1.99.99", 1},
		{"1.2.3", "1.2.3", 0},
	}
	for _, c := range cases {
		if got := compareTexts(t, c.a, c.b); got != c.want {
			t.Errorf("Compare(%s, %s) = %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

func TestComparesPrereleaseBelowRelease(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0.0-alpha", "1.0.0", -1},
		{"1.0.0", "1.0.0-rc.1", 1},
		{"1.0.1-alpha", "1.0.0", 1},
	}
	for _, c := range cases {
		if got := compareTexts(t, c.a, c.b); got != c.want {
			t.Errorf("Compare(%s, %s) = %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

func TestFollowsTheSpecExample(t *testing.T) {
	ordered := []string{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"}
	for i := 0; i+1 < len(ordered); i++ {
		if got := compareTexts(t, ordered[i], ordered[i+1]); got != -1 {
			t.Errorf("Compare(%s, %s) = %d", ordered[i], ordered[i+1], got)
		}
		if got := compareTexts(t, ordered[i+1], ordered[i]); got != 1 {
			t.Errorf("Compare(%s, %s) = %d", ordered[i+1], ordered[i], got)
		}
	}
}

func TestComparesIdentifiers(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0.0-9", "1.0.0-10", -1},
		{"1.0.0-alpha.10", "1.0.0-alpha.9", 1},
		{"1.0.0-99999999999999999999", "1.0.0-100000000000000000000", -1},
		{"1.0.0-1", "1.0.0-alpha", -1},
		{"1.0.0-0a", "1.0.0-999", 1},
		{"1.0.0-Beta", "1.0.0-alpha", -1},
		{"1.0.0-alpha", "1.0.0-alpha.0", -1},
		{"1.0.0-alpha.beta.1", "1.0.0-alpha.beta", 1},
		{"1.0.0-rc.1", "1.0.0-rc.1", 0},
	}
	for _, c := range cases {
		if got := compareTexts(t, c.a, c.b); got != c.want {
			t.Errorf("Compare(%s, %s) = %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

func TestIgnoresBuildMetadata(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"1.0.0+a", "1.0.0+b", 0},
		{"1.0.0-rc.1+build.9", "1.0.0-rc.1", 0},
		{"1.0.0-rc.1+zzz", "1.0.0", -1},
	}
	for _, c := range cases {
		if got := compareTexts(t, c.a, c.b); got != c.want {
			t.Errorf("Compare(%s, %s) = %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

func TestSortsByPrecedenceKeepingTies(t *testing.T) {
	var versions []Version
	for _, text := range []string{"1.10.0", "1.0.0+b", "1.0.0-rc.1", "0.9.0", "1.0.0+a", "1.0.0", "1.2.0"} {
		versions = append(versions, mustParse(t, text))
	}
	SortVersions(versions)
	var got []string
	for _, v := range versions {
		got = append(got, v.String())
	}
	if want := []string{"0.9.0", "1.0.0-rc.1", "1.0.0+b", "1.0.0+a", "1.0.0", "1.2.0", "1.10.0"}; !slices.Equal(got, want) {
		t.Errorf("got %q, want %q", got, want)
	}
	SortVersions(nil)
}
