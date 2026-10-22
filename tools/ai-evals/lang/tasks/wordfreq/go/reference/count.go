package main

import (
	"sort"
	"strconv"
	"strings"
)

// WordCount is a word and how often it occurs.
type WordCount struct {
	Word  string
	Count int
}

func isLetter(c byte) bool { return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' }

func lower(c byte) byte {
	if c >= 'A' && c <= 'Z' {
		return c + 'a' - 'A'
	}
	return c
}

// CountWords counts the ASCII-letter words of text, lowercased.
func CountWords(text []byte) map[string]int {
	counts := make(map[string]int)
	word := make([]byte, 0, 64)
	for _, c := range text {
		if isLetter(c) {
			word = append(word, lower(c))
			continue
		}
		if len(word) > 0 {
			counts[string(word)]++
			word = word[:0]
		}
	}
	if len(word) > 0 {
		counts[string(word)]++
	}
	return counts
}

// TopWords returns at most limit words, the most frequent first, equal counts by word.
func TopWords(counts map[string]int, limit int) []WordCount {
	ranked := make([]WordCount, 0, len(counts))
	for word, count := range counts {
		ranked = append(ranked, WordCount{word, count})
	}
	sort.Slice(ranked, func(i, j int) bool {
		if ranked[i].Count != ranked[j].Count {
			return ranked[i].Count > ranked[j].Count
		}
		return ranked[i].Word < ranked[j].Word
	})
	if len(ranked) > limit {
		ranked = ranked[:limit]
	}
	return ranked
}

// Render writes one "word count" line per entry.
func Render(ranked []WordCount) string {
	var out strings.Builder
	for _, entry := range ranked {
		out.WriteString(entry.Word)
		out.WriteByte(' ')
		out.WriteString(strconv.Itoa(entry.Count))
		out.WriteByte('\n')
	}
	return out.String()
}
