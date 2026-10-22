package main

import (
	"slices"
	"strconv"
	"strings"
)

const fieldsPerLine = 3

// Roster is the students read from a CSV file; BadLine is the number of the line that stopped it, 0 when every line read.
type Roster struct {
	Students []Student
	BadLine  int
}

// ScoreResult is the ids of unknown students, in the order they were met, and the line that stopped the scores, else 0.
type ScoreResult struct {
	UnknownIDs []string
	BadLine    int
}

type scoreLine struct {
	id    string
	kind  Kind
	score int64
}

// eachRecord calls each for every non-empty line after the header; it stops when each returns false and gives that
// line's number, else 0.
func eachRecord(text string, each func(line string) bool) int {
	for index, line := range strings.Split(text, "\n") {
		line = strings.TrimSuffix(line, "\r")
		if index > 0 && line != "" && !each(line) {
			return index + 1
		}
	}
	return 0
}

// fields splits a line into its comma-separated fields; false unless there are exactly three.
func fields(line string) ([]string, bool) {
	parts := strings.Split(line, ",")
	return parts, len(parts) == fieldsPerLine
}

func parseKind(text string) (Kind, bool) {
	switch text {
	case "homework":
		return Homework, true
	case "quiz":
		return Quiz, true
	case "exam":
		return Exam, true
	}
	return 0, false
}

// parseScore reads a whole number from 0 to maxScore written as digits, with an optional '-' and no '+'.
func parseScore(text string) (int64, bool) {
	if strings.HasPrefix(text, "+") {
		return 0, false
	}
	score, err := strconv.ParseInt(text, 10, 64)
	return score, err == nil && score >= 0 && score <= maxScore
}

func parseScoreLine(line string) (scoreLine, bool) {
	parts, ok := fields(line)
	if !ok || parts[0] == "" {
		return scoreLine{}, false
	}
	kind, kindOK := parseKind(parts[1])
	score, scoreOK := parseScore(parts[2])
	return scoreLine{id: parts[0], kind: kind, score: score}, kindOK && scoreOK
}

// ParseStudents reads "id,name,section" lines after a header: three fields, none empty.
func ParseStudents(text string) Roster {
	var roster Roster
	roster.BadLine = eachRecord(text, func(line string) bool {
		parts, ok := fields(line)
		if !ok || slices.Contains(parts, "") {
			return false
		}
		roster.Students = append(roster.Students, Student{ID: parts[0], Name: parts[1], Section: parts[2]})
		return true
	})
	return roster
}

// indexByID maps each id to its student; of students sharing an id, the last one.
func indexByID(students []Student) map[string]int {
	index := make(map[string]int, len(students))
	for i, student := range students {
		index[student.ID] = i
	}
	return index
}

// ApplyScores adds "student_id,kind,score" lines after a header to the students' tallies; unknown ids are noted and
// skipped.
func ApplyScores(text string, students []Student) ScoreResult {
	var result ScoreResult
	index := indexByID(students)
	result.BadLine = eachRecord(text, func(line string) bool {
		parsed, ok := parseScoreLine(line)
		if !ok {
			return false
		}
		at, known := index[parsed.id]
		if !known {
			result.UnknownIDs = append(result.UnknownIDs, parsed.id)
			return true
		}
		students[at].Tallies[parsed.kind].Add(parsed.score)
		return true
	})
	return result
}
