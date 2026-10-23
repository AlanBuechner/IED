package main

import "fmt"

// Kind is what a score was for.
type Kind int

const (
	Homework Kind = iota
	Quiz
	Exam
	kinds
)

const (
	maxScore           = 100
	hundredthsPerPoint = 100
	percent            = 100
	homeworkWeight     = 30
	quizWeight         = 20
	examWeight         = 50
	lowestA            = 9000
	lowestB            = 8000
	lowestC            = 7000
	lowestD            = 6000
	hundredthsPerTenth = 10
	tenthsPerPoint     = 10
)

// Tally is the scores of one kind: their sum and how many.
type Tally struct {
	Sum   int64
	Count int64
}

// Add counts one more score.
func (tally *Tally) Add(score int64) {
	tally.Sum += score
	tally.Count++
}

type Student struct {
	ID      string
	Name    string
	Section string
	Tallies [kinds]Tally
}

// RoundedMean is the mean of count values whose sum is sum, rounded half up; 0 when there are none.
func RoundedMean(sum, count int64) int64 {
	if count == 0 {
		return 0
	}
	return (sum*2 + count) / (2 * count)
}

// TallyAverage is the average of a tally in hundredths of a point; 0 for no scores.
func TallyAverage(tally Tally) int64 {
	return RoundedMean(tally.Sum*hundredthsPerPoint, tally.Count)
}

// WeightedAverage weighs homework 30%, quizzes 20% and exams 50%, in hundredths of a point, rounded half up.
func WeightedAverage(student Student) int64 {
	weights := [kinds]int64{homeworkWeight, quizWeight, examWeight}
	var weighted int64
	for kind, tally := range student.Tallies {
		weighted += TallyAverage(tally) * weights[kind]
	}
	return (weighted + percent/2) / percent
}

// Letter is A for 90 and up, then B, C, D at 80, 70, 60; F below.
func Letter(average int64) string {
	switch {
	case average >= lowestA:
		return "A"
	case average >= lowestB:
		return "B"
	case average >= lowestC:
		return "C"
	case average >= lowestD:
		return "D"
	}
	return "F"
}

// FormatTenths writes hundredths with one decimal, rounded half up: 8575 is "85.8".
func FormatTenths(hundredths int64) string {
	tenths := (hundredths + hundredthsPerTenth/2) / hundredthsPerTenth
	return fmt.Sprintf("%d.%d", tenths/tenthsPerPoint, tenths%tenthsPerPoint)
}
