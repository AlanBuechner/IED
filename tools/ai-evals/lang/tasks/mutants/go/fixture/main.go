package main

import (
	"fmt"
	"os"
	"strings"
)

const (
	exitError = 1
	exitUsage = 2
)

func usage() int {
	fmt.Fprintln(os.Stderr, "usage: semver compare <a> <b> | semver satisfies <version> <range> | semver sort <file>")
	return exitUsage
}

func fail(err error) int {
	fmt.Fprintln(os.Stderr, "error: "+err.Error())
	return exitError
}

func compareCommand(first, second string) int {
	a, err := ParseVersion(first)
	if err != nil {
		return fail(err)
	}
	b, err := ParseVersion(second)
	if err != nil {
		return fail(err)
	}
	fmt.Println(Compare(a, b))
	return 0
}

func satisfiesCommand(versionText, rangeText string) int {
	v, err := ParseVersion(versionText)
	if err != nil {
		return fail(err)
	}
	r, err := ParseRange(rangeText)
	if err != nil {
		return fail(err)
	}
	if r.Satisfies(v) {
		fmt.Println("yes")
	} else {
		fmt.Println("no")
	}
	return 0
}

func sortCommand(path string) int {
	text, err := os.ReadFile(path)
	if err != nil {
		return fail(fmt.Errorf("cannot read %s", path))
	}
	var versions []Version
	for _, line := range strings.Split(string(text), "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		v, err := ParseVersion(line)
		if err != nil {
			return fail(err)
		}
		versions = append(versions, v)
	}
	SortVersions(versions)
	var out strings.Builder
	for _, v := range versions {
		out.WriteString(v.String())
		out.WriteByte('\n')
	}
	os.Stdout.WriteString(out.String())
	return 0
}

func run(args []string) int {
	switch {
	case len(args) == 3 && args[0] == "compare":
		return compareCommand(args[1], args[2])
	case len(args) == 3 && args[0] == "satisfies":
		return satisfiesCommand(args[1], args[2])
	case len(args) == 2 && args[0] == "sort":
		return sortCommand(args[1])
	}
	return usage()
}

func main() {
	os.Exit(run(os.Args[1:]))
}
