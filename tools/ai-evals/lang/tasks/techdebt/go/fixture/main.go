package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

type Student struct {
	id        string
	name      string
	section   string
	hwSum     int64
	hwCount   int64
	quizSum   int64
	quizCount int64
	examSum   int64
	examCount int64
	avg       int64
}

var students []Student
var warnings int64 = 0
var errorText string

func kindAvg(sum int64, count int64) int64 {
	if count == 0 {
		return 0
	}
	return (sum*100*2 + count) / (2 * count)
}

func printTenths(out *strings.Builder, hundredths int64) {
	t := (hundredths + 5) / 10
	out.WriteString(strconv.FormatInt(t/10, 10))
	out.WriteString(".")
	out.WriteString(strconv.FormatInt(t%10, 10))
}

func main() {
	if len(os.Args) != 3 {
		os.Stderr.WriteString("usage: gradebook <students.csv> <scores.csv>\n")
		os.Exit(2)
	}
	// read students
	text1, err := os.ReadFile(os.Args[1])
	if err != nil {
		errorText = "error: cannot read " + os.Args[1] + "\n"
		os.Stderr.WriteString(errorText)
		os.Exit(1)
	}
	rest := string(text1)
	lineNo := 0
	for len(rest) > 0 {
		e := strings.IndexByte(rest, '\n')
		if e < 0 {
			e = len(rest)
		}
		end := e
		if e > 0 && rest[e-1] == '\r' {
			end = e - 1
		}
		line := rest[:end]
		rest = rest[min(e+1, len(rest)):]
		lineNo++
		if lineNo == 1 || line == "" {
			continue
		}
		c1 := strings.IndexByte(line, ',')
		c2 := -1
		if c1 >= 0 && strings.IndexByte(line[c1+1:], ',') >= 0 {
			c2 = c1 + 1 + strings.IndexByte(line[c1+1:], ',')
		}
		if c1 <= 0 || c2 < 0 || strings.IndexByte(line[c2+1:], ',') >= 0 || c2 == c1+1 || c2 == len(line)-1 {
			errorText = fmt.Sprintf("error: %s line %d\n", os.Args[1], lineNo)
			os.Stderr.WriteString(errorText)
			os.Exit(1)
		}
		var s Student
		s.id = line[:c1]
		s.name = line[c1+1 : c2]
		s.section = line[c2+1:]
		students = append(students, s)
	}
	// read scores
	text2, err := os.ReadFile(os.Args[2])
	if err != nil {
		errorText = "error: cannot read " + os.Args[2] + "\n"
		os.Stderr.WriteString(errorText)
		os.Exit(1)
	}
	rest = string(text2)
	lineNo = 0
	for len(rest) > 0 {
		e := strings.IndexByte(rest, '\n')
		if e < 0 {
			e = len(rest)
		}
		end := e
		if e > 0 && rest[e-1] == '\r' {
			end = e - 1
		}
		line := rest[:end]
		rest = rest[min(e+1, len(rest)):]
		lineNo++
		if lineNo == 1 || line == "" {
			continue
		}
		c1 := strings.IndexByte(line, ',')
		c2 := -1
		if c1 >= 0 && strings.IndexByte(line[c1+1:], ',') >= 0 {
			c2 = c1 + 1 + strings.IndexByte(line[c1+1:], ',')
		}
		var score int64 = 0
		ok := c1 > 0 && c2 > c1+1 && strings.IndexByte(line[c2+1:], ',') < 0
		if ok {
			n, perr := strconv.ParseInt(line[c2+1:], 10, 64)
			ok = perr == nil && line[c2+1] != '+' && n >= 0 && n <= 100
			score = n
		}
		kind := ""
		if ok {
			kind = line[c1+1 : c2]
		}
		if !ok || (kind != "homework" && kind != "quiz" && kind != "exam") {
			errorText = fmt.Sprintf("error: %s line %d\n", os.Args[2], lineNo)
			os.Stderr.WriteString(errorText)
			os.Exit(1)
		}
		id := line[:c1]
		found := -1
		for i := 0; i < len(students); i++ {
			if students[i].id == id {
				found = i
			}
		}
		if found < 0 {
			os.Stderr.WriteString("warning: unknown student " + id + "\n")
			warnings++
			continue
		}
		if kind == "homework" {
			students[found].hwSum += score
			students[found].hwCount++
		}
		if kind == "quiz" {
			students[found].quizSum += score
			students[found].quizCount++
		}
		if kind == "exam" {
			students[found].examSum += score
			students[found].examCount++
		}
	}
	// compute
	for i := 0; i < len(students); i++ {
		s := &students[i]
		s.avg = (kindAvg(s.hwSum, s.hwCount)*30 + kindAvg(s.quizSum, s.quizCount)*20 + kindAvg(s.examSum, s.examCount)*50 + 50) / 100
	}
	// sections
	var sections []string
	for i := 0; i < len(students); i++ {
		have := false
		for j := 0; j < len(sections); j++ {
			if sections[j] == students[i].section {
				have = true
			}
		}
		if !have {
			sections = append(sections, students[i].section)
		}
	}
	for i := 0; i < len(sections); i++ {
		for j := 0; j+1 < len(sections)-i; j++ {
			if sections[j+1] < sections[j] {
				t := sections[j]
				sections[j] = sections[j+1]
				sections[j+1] = t
			}
		}
	}
	var out strings.Builder
	var total int64 = 0
	for k := 0; k < len(sections); k++ {
		var list []Student
		for i := 0; i < len(students); i++ {
			if students[i].section == sections[k] {
				list = append(list, students[i])
			}
		}
		for i := 0; i < len(list); i++ {
			for j := 0; j+1 < len(list)-i; j++ {
				if list[j+1].avg > list[j].avg || (list[j+1].avg == list[j].avg && list[j+1].name < list[j].name) {
					t := list[j]
					list[j] = list[j+1]
					list[j+1] = t
				}
			}
		}
		out.WriteString("section ")
		out.WriteString(sections[k])
		out.WriteString("\n")
		var sum int64 = 0
		for i := 0; i < len(list); i++ {
			out.WriteString("  ")
			out.WriteString(list[i].name)
			out.WriteString(" ")
			printTenths(&out, list[i].avg)
			if list[i].avg >= 9000 {
				out.WriteString(" A\n")
			} else if list[i].avg >= 8000 {
				out.WriteString(" B\n")
			} else if list[i].avg >= 7000 {
				out.WriteString(" C\n")
			} else if list[i].avg >= 6000 {
				out.WriteString(" D\n")
			} else {
				out.WriteString(" F\n")
			}
			sum += list[i].avg
		}
		total += sum
		n := int64(len(list))
		var median int64
		if n%2 == 1 {
			median = list[n/2].avg
		} else {
			median = (list[n/2-1].avg + list[n/2].avg + 1) / 2
		}
		out.WriteString("  mean ")
		printTenths(&out, (sum*2+n)/(2*n))
		out.WriteString(" median ")
		printTenths(&out, median)
		out.WriteString("\n")
	}
	out.WriteString("overall mean ")
	if len(students) == 0 {
		printTenths(&out, 0)
	} else {
		printTenths(&out, (total*2+int64(len(students)))/(2*int64(len(students))))
	}
	out.WriteString("\nhonor roll: ")
	var honor []string
	for i := 0; i < len(students); i++ {
		if students[i].avg >= 9000 {
			honor = append(honor, students[i].name)
		}
	}
	for i := 0; i < len(honor); i++ {
		for j := 0; j+1 < len(honor)-i; j++ {
			if honor[j+1] < honor[j] {
				t := honor[j]
				honor[j] = honor[j+1]
				honor[j+1] = t
			}
		}
	}
	if len(honor) == 0 {
		out.WriteString("none")
	}
	for i := 0; i < len(honor); i++ {
		if i > 0 {
			out.WriteString(", ")
		}
		out.WriteString(honor[i])
	}
	out.WriteString("\n")
	os.Stdout.WriteString(out.String())
	if warnings > 0 {
		fmt.Fprintf(os.Stderr, "%d warning(s)\n", warnings)
	}
}
