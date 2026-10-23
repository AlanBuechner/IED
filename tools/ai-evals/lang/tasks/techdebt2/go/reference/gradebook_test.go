package main

import "testing"

func TestWeightsTheKinds(t *testing.T) {
	var student Student
	student.Tallies[Homework] = Tally{Sum: 180, Count: 2}
	student.Tallies[Quiz] = Tally{Sum: 80, Count: 1}
	student.Tallies[Exam] = Tally{Sum: 70, Count: 1}
	if got := WeightedAverage(student); got != 7800 {
		t.Errorf("30%% of 90, 20%% of 80, 50%% of 70: got %d", got)
	}
	if got := WeightedAverage(Student{}); got != 0 {
		t.Errorf("no scores: got %d", got)
	}
}

func TestLettersAtTheBounds(t *testing.T) {
	want := map[int64]string{10000: "A", 9000: "A", 8999: "B", 8000: "B", 7000: "C", 6000: "D", 5999: "F", 0: "F"}
	for average, letter := range want {
		if got := Letter(average); got != letter {
			t.Errorf("Letter(%d) = %q, want %q", average, got, letter)
		}
	}
}

func TestRoundsTenthsHalfUp(t *testing.T) {
	want := map[int64]string{8575: "85.8", 8574: "85.7", 0: "0.0", 10000: "100.0"}
	for hundredths, text := range want {
		if got := FormatTenths(hundredths); got != text {
			t.Errorf("FormatTenths(%d) = %q, want %q", hundredths, got, text)
		}
	}
	if got := RoundedMean(3, 2); got != 2 {
		t.Errorf("mean of 1 and 2 rounds half up: got %d", got)
	}
}

func TestFindsBadLines(t *testing.T) {
	if got := ParseStudents("id,name,section\n1,a,x\n2,,x\n").BadLine; got != 3 {
		t.Errorf("empty name: line %d", got)
	}
	if got := ParseStudents("id,name,section\n1,a,x,y\n").BadLine; got != 2 {
		t.Errorf("four fields: line %d", got)
	}
	students := ParseStudents("h\n1,a,x\n").Students
	for _, text := range []string{"h\n1,lab,50\n", "h\n1,exam,101\n", "h\n1,exam,-1\n", "h\n1,exam,+5\n", "h\n,exam,5\n", "h\n1,exam\n"} {
		if got := ApplyScores(text, students).BadLine; got != 2 {
			t.Errorf("%q: line %d", text, got)
		}
	}
}

func TestSkipsHeaderBlankLinesAndCarriageReturns(t *testing.T) {
	roster := ParseStudents("1,header,row\r\n\r\n1,a,x\r\n\n2,b,y")
	if roster.BadLine != 0 || len(roster.Students) != 2 || roster.Students[0].Section != "x" || roster.Students[1].Section != "y" {
		t.Fatalf("got %+v", roster)
	}
}

func TestSkipsUnknownStudents(t *testing.T) {
	students := ParseStudents("h\n1,a,x\n").Students
	result := ApplyScores("h\n9,exam,50\n1,exam,70\n", students)
	if len(result.UnknownIDs) != 1 || result.UnknownIDs[0] != "9" {
		t.Errorf("unknown ids: %v", result.UnknownIDs)
	}
	if got := students[0].Tallies[Exam]; got != (Tally{Sum: 70, Count: 1}) {
		t.Errorf("the known score counted: got %+v", got)
	}
}

func TestScoresGoToTheLastStudentOfAnID(t *testing.T) {
	students := ParseStudents("h\n1,a,x\n1,b,x\n").Students
	ApplyScores("h\n1,quiz,40\n", students)
	if students[0].Tallies[Quiz].Count != 0 || students[1].Tallies[Quiz].Sum != 40 {
		t.Errorf("got %+v", students)
	}
}

func TestReportsSectionsInOrder(t *testing.T) {
	students := ParseStudents("h\n1,Zed,b\n2,Amy,a\n3,Bob,a\n").Students
	ApplyScores("h\n1,exam,100\n1,quiz,100\n1,homework,100\n2,exam,50\n3,exam,70\n", students)
	want := "section a\n  Bob 35.0 F\n  Amy 25.0 F\n  mean 30.0\n" +
		"section b\n  Zed 100.0 A\n  mean 100.0\n" +
		"overall mean 53.3\n"
	if got := Report(students); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestReportsNoStudents(t *testing.T) {
	if got := Report(nil); got != "overall mean 0.0\n" {
		t.Errorf("got %q", got)
	}
}
