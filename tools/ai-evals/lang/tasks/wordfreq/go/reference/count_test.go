package main

import "testing"

func TestCountFoldsCaseAndSplits(t *testing.T) {
	counts := CountWords([]byte("Dog dog, DOG9cat"))
	if len(counts) != 2 || counts["dog"] != 3 || counts["cat"] != 1 {
		t.Fatalf("got %v", counts)
	}
}

func TestRankTiesByWord(t *testing.T) {
	ranked := TopWords(CountWords([]byte("b a c b a")), 2)
	if len(ranked) != 2 || ranked[0].Word != "a" || ranked[1].Word != "b" {
		t.Fatalf("got %v", ranked)
	}
}
