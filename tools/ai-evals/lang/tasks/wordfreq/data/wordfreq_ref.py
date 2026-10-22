"""The reference answer of the wordfreq task: the N most frequent ASCII-letter words of a file."""

import re
import sys

WORD = re.compile(rb"[A-Za-z]+")


def top_words(data, n):
	counts = {}
	for word in WORD.findall(data):
		word = word.lower()
		counts[word] = counts.get(word, 0) + 1
	ranked = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
	return ranked[:n]


def render(ranked):
	return "".join("%s %d\n" % (word.decode("ascii"), count) for word, count in ranked)


if __name__ == "__main__":
	with open(sys.argv[1], "rb") as f:
		sys.stdout.write(render(top_words(f.read(), int(sys.argv[2]) if len(sys.argv) > 2 else 10)))
