package main

import (
	"fmt"
	"slices"
	"sort"
	"strings"
)

type ranked struct {
	name    string
	section string
	average int64
}

// rankAll is every student's average, best first, equal averages by name.
func rankAll(students []Student) []ranked {
	all := make([]ranked, 0, len(students))
	for _, student := range students {
		all = append(all, ranked{name: student.Name, section: student.Section, average: WeightedAverage(student)})
	}
	sort.SliceStable(all, func(i, j int) bool {
		if all[i].average != all[j].average {
			return all[i].average > all[j].average
		}
		return all[i].name < all[j].name
	})
	return all
}

func sectionNames(all []ranked) []string {
	seen := make(map[string]bool)
	var names []string
	for _, each := range all {
		if !seen[each.section] {
			seen[each.section] = true
			names = append(names, each.section)
		}
	}
	slices.Sort(names)
	return names
}

func writeSection(out *strings.Builder, section string, all []ranked) {
	var count, sum int64
	fmt.Fprintf(out, "section %s\n", section)
	for _, each := range all {
		if each.section != section {
			continue
		}
		fmt.Fprintf(out, "  %s %s %s\n", each.name, FormatTenths(each.average), Letter(each.average))
		count++
		sum += each.average
	}
	fmt.Fprintf(out, "  mean %s\n", FormatTenths(RoundedMean(sum, count)))
}

// Report is each section by name with its students by average (best first, then by name) and its mean; then the
// overall mean.
func Report(students []Student) string {
	all := rankAll(students)
	var out strings.Builder
	var total int64
	for _, section := range sectionNames(all) {
		writeSection(&out, section, all)
	}
	for _, each := range all {
		total += each.average
	}
	fmt.Fprintf(&out, "overall mean %s\n", FormatTenths(RoundedMean(total, int64(len(all)))))
	return out.String()
}
