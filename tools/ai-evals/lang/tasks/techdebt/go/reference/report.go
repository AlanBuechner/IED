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

// MedianOfRanked is the middle of averages sorted best first, or the two middle ones' mean rounded half up.
func MedianOfRanked(averages []int64) int64 {
	middle := len(averages) / 2
	if len(averages)%2 == 1 {
		return averages[middle]
	}
	return (averages[middle-1] + averages[middle] + 1) / 2
}

func writeSection(out *strings.Builder, section string, all []ranked) {
	var averages []int64
	var sum int64
	fmt.Fprintf(out, "section %s\n", section)
	for _, each := range all {
		if each.section != section {
			continue
		}
		fmt.Fprintf(out, "  %s %s %s\n", each.name, FormatTenths(each.average), Letter(each.average))
		averages = append(averages, each.average)
		sum += each.average
	}
	mean := RoundedMean(sum, int64(len(averages)))
	fmt.Fprintf(out, "  mean %s median %s\n", FormatTenths(mean), FormatTenths(MedianOfRanked(averages)))
}

func writeHonorRoll(out *strings.Builder, all []ranked) {
	var names []string
	for _, each := range all {
		if Letter(each.average) == "A" {
			names = append(names, each.name)
		}
	}
	slices.Sort(names)
	roll := strings.Join(names, ", ")
	if roll == "" {
		roll = "none"
	}
	fmt.Fprintf(out, "honor roll: %s\n", roll)
}

// Report is each section by name with its students by average (best first, then by name), its mean and median; then
// the overall mean and the honor roll.
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
	writeHonorRoll(&out, all)
	return out.String()
}
