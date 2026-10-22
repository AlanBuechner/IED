package main

import "testing"

func TestClassifiesLines(t *testing.T) {
	cases := []struct {
		line string
		kind LineKind
	}{
		{"   ", LineBlank},
		{"```go", LineFence},
		{"### Title", LineHeading},
		{"- - -", LineRule},
		{"  > quoted", LineQuote},
		{"12. item", LineListItem},
		{"plain text", LineText},
	}
	for _, c := range cases {
		if got := Classify(c.line); got != c.kind {
			t.Errorf("%q: kind %d, want %d", c.line, got, c.kind)
		}
	}
}

func TestReadsFences(t *testing.T) {
	fence, ok := MatchFence("  ````python extra words")
	if !ok || fence.Ticks != 4 || fence.Language != "python" {
		t.Errorf("got %+v, %v", fence, ok)
	}
	if fence, ok := MatchFence("```"); !ok || fence.Language != "" {
		t.Errorf("bare fence: %+v, %v", fence, ok)
	}
	for _, line := range []string{"``", "```a`b"} {
		if _, ok := MatchFence(line); ok {
			t.Errorf("%q is a fence", line)
		}
	}
}

func TestClosesFencesWithEnoughTicks(t *testing.T) {
	if !ClosesFence("````", 3) || !ClosesFence("  ```  ", 3) || ClosesFence("```", 4) || ClosesFence("``` x", 3) {
		t.Error("ClosesFence")
	}
}

func TestReadsHeadings(t *testing.T) {
	heading, ok := MatchHeading("##   Title  ")
	if !ok || heading.Level != 2 || heading.Text != "Title" {
		t.Errorf("got %+v, %v", heading, ok)
	}
	if heading, ok := MatchHeading("######"); !ok || heading.Level != 6 {
		t.Errorf("six: %+v, %v", heading, ok)
	}
	for _, line := range []string{"####### seven", "#hashtag"} {
		if _, ok := MatchHeading(line); ok {
			t.Errorf("%q is a heading", line)
		}
	}
}

func TestStripsClosingHashes(t *testing.T) {
	cases := []struct{ line, text string }{
		{"# Title ###", "Title"},
		{"# C#", "C#"},
		{"# ###", ""},
		{"# a \\#", "a \\#"},
	}
	for _, c := range cases {
		if heading, _ := MatchHeading(c.line); heading.Text != c.text {
			t.Errorf("%q: got %q, want %q", c.line, heading.Text, c.text)
		}
	}
}

func TestReadsRules(t *testing.T) {
	if !IsRule("---") || !IsRule(" * * * * ") || IsRule("--") || IsRule("-*-") || IsRule("--- x") {
		t.Error("IsRule")
	}
}

func TestReadsQuotes(t *testing.T) {
	cases := []struct{ line, content string }{
		{"> text", "text"},
		{">text", "text"},
		{">  two", " two"},
		{"> > nested", "> nested"},
	}
	for _, c := range cases {
		if content, ok := MatchQuote(c.line); !ok || content != c.content {
			t.Errorf("%q: got %q, %v", c.line, content, ok)
		}
	}
	if _, ok := MatchQuote("quote >"); ok {
		t.Error("not a quote")
	}
}

func TestReadsListMarkers(t *testing.T) {
	bullet, ok := MatchListItem("    *   star ")
	if !ok || bullet.Indent != 4 || bullet.Ordered || bullet.Text != "star" {
		t.Errorf("bullet: %+v, %v", bullet, ok)
	}
	numbered, ok := MatchListItem("007. bond")
	if !ok || !numbered.Ordered || numbered.Number != 7 {
		t.Errorf("numbered: %+v, %v", numbered, ok)
	}
	if empty, ok := MatchListItem("-"); !ok || empty.Text != "" {
		t.Errorf("empty: %+v, %v", empty, ok)
	}
}

func TestRejectsWhatIsNoListMarker(t *testing.T) {
	for _, line := range []string{"-item", "**bold**", "1.5 pounds", "1) one", "1234567890. ten digits"} {
		if _, ok := MatchListItem(line); ok {
			t.Errorf("%q is a list item", line)
		}
	}
}
