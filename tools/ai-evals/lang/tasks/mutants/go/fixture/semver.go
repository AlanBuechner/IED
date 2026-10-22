package main

import (
	"cmp"
	"fmt"
	"slices"
	"strconv"
	"strings"
)

// Version is a semantic version (semver.org 2.0.0): MAJOR.MINOR.PATCH, then pre-release identifiers after a "-" and
// build metadata identifiers after a "+".
type Version struct {
	Major, Minor, Patch int64
	Prerelease          []string
	Build               []string
}

func isDigit(c byte) bool {
	return c >= '0' && c <= '9'
}

func isIdentifierChar(c byte) bool {
	return isDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-'
}

func isNumeric(text string) bool {
	for i := 0; i < len(text); i++ {
		if !isDigit(text[i]) {
			return false
		}
	}
	return text != ""
}

func hasLeadingZero(text string) bool {
	return len(text) > 1 && text[0] == '0'
}

func isIdentifier(text string) bool {
	for i := 0; i < len(text); i++ {
		if !isIdentifierChar(text[i]) {
			return false
		}
	}
	return text != ""
}

func isPrereleaseIdentifier(text string) bool {
	return isIdentifier(text) && !(isNumeric(text) && hasLeadingZero(text))
}

func parseNumber(text string) (int64, bool) {
	if !isNumeric(text) || hasLeadingZero(text) {
		return 0, false
	}
	n, err := strconv.ParseInt(text, 10, 64)
	return n, err == nil
}

func parseCore(text string, v *Version) bool {
	parts := strings.Split(text, ".")
	if len(parts) != 3 {
		return false
	}
	var majorOK, minorOK, patchOK bool
	v.Major, majorOK = parseNumber(parts[0])
	v.Minor, minorOK = parseNumber(parts[1])
	v.Patch, patchOK = parseNumber(parts[2])
	return majorOK && minorOK && patchOK
}

func splitIdentifiers(text string, valid func(string) bool) ([]string, bool) {
	identifiers := strings.Split(text, ".")
	for _, identifier := range identifiers {
		if !valid(identifier) {
			return nil, false
		}
	}
	return identifiers, true
}

// ParseVersion reads "1.2.3", "1.0.0-alpha.1", "1.0.0+build.5" or "1.0.0-rc.1+build.5"; the error says the text is
// not a valid version.
func ParseVersion(text string) (Version, error) {
	var v Version
	rest, build, hasBuild := strings.Cut(text, "+")
	core, prerelease, hasPrerelease := strings.Cut(rest, "-")
	ok := parseCore(core, &v)
	if ok && hasPrerelease {
		v.Prerelease, ok = splitIdentifiers(prerelease, isPrereleaseIdentifier)
	}
	if ok && hasBuild {
		v.Build, ok = splitIdentifiers(build, isIdentifier)
	}
	if !ok {
		return Version{}, fmt.Errorf("invalid version %s", text)
	}
	return v, nil
}

// String writes the version as text: "1.0.0-rc.1+build.5".
func (v Version) String() string {
	var b strings.Builder
	fmt.Fprintf(&b, "%d.%d.%d", v.Major, v.Minor, v.Patch)
	if len(v.Prerelease) > 0 {
		b.WriteString("-" + strings.Join(v.Prerelease, "."))
	}
	if len(v.Build) > 0 {
		b.WriteString("+" + strings.Join(v.Build, "."))
	}
	return b.String()
}

// compareNumeric compares digit strings without leading zeros by value, whatever their length.
func compareNumeric(a, b string) int {
	return cmp.Or(cmp.Compare(len(a), len(b)), strings.Compare(a, b))
}

func compareIdentifiers(a, b string) int {
	aNumeric, bNumeric := isNumeric(a), isNumeric(b)
	switch {
	case aNumeric && bNumeric:
		return compareNumeric(a, b)
	case aNumeric:
		return -1
	case bNumeric:
		return 1
	}
	return strings.Compare(a, b)
}

func comparePrerelease(a, b []string) int {
	if len(a) == 0 || len(b) == 0 {
		return cmp.Compare(len(b), len(a))
	}
	for i := 0; i < len(a) && i < len(b); i++ {
		if order := compareIdentifiers(a[i], b[i]); order != 0 {
			return order
		}
	}
	return cmp.Compare(len(a), len(b))
}

// Compare is -1, 0 or 1 as a has lower, the same or higher precedence than b (semver.org section 11); build metadata
// does not count.
func Compare(a, b Version) int {
	return cmp.Or(
		cmp.Compare(a.Major, b.Major),
		cmp.Compare(a.Minor, b.Minor),
		cmp.Compare(a.Patch, b.Patch),
		comparePrerelease(a.Prerelease, b.Prerelease),
	)
}

// SortVersions orders the versions by ascending precedence; versions of the same precedence keep their order.
func SortVersions(versions []Version) {
	slices.SortStableFunc(versions, Compare)
}
