"""The reference of the spans task: a set of integers as disjoint half-open spans, driven by stdin commands."""

import bisect
import sys


class SpanSet:
	def __init__(self):
		self.starts = []
		self.ends = []

	def add(self, start, end):
		if end <= start:
			return
		i = bisect.bisect_left(self.ends, start)
		j = bisect.bisect_right(self.starts, end)
		if i < j:
			start = min(start, self.starts[i])
			end = max(end, self.ends[j - 1])
		self.starts[i:j] = [start]
		self.ends[i:j] = [end]

	def remove(self, start, end):
		if end <= start:
			return
		i = bisect.bisect_right(self.ends, start)
		j = bisect.bisect_left(self.starts, end)
		if i >= j:
			return
		keep_s, keep_e = [], []
		if self.starts[i] < start:
			keep_s.append(self.starts[i])
			keep_e.append(start)
		if self.ends[j - 1] > end:
			keep_s.append(end)
			keep_e.append(self.ends[j - 1])
		self.starts[i:j] = keep_s
		self.ends[i:j] = keep_e

	def contains(self, point):
		i = bisect.bisect_right(self.starts, point) - 1
		return i >= 0 and point < self.ends[i]

	def count(self):
		return len(self.starts)

	def covered(self):
		return sum(e - s for s, e in zip(self.starts, self.ends))


def run(lines, out):
	spans = SpanSet()
	for number, line in enumerate(lines, 1):
		words = line.split()
		if not words:
			continue
		try:
			if words[0] in ("add", "remove") and len(words) == 3:
				getattr(spans, words[0])(int(words[1]), int(words[2]))
			elif words[0] == "contains" and len(words) == 2:
				out.append("yes" if spans.contains(int(words[1])) else "no")
			elif words[0] == "count" and len(words) == 1:
				out.append(str(spans.count()))
			elif words[0] == "covered" and len(words) == 1:
				out.append(str(spans.covered()))
			elif words[0] == "list" and len(words) == 1:
				out.append(" ".join("[%d,%d)" % (s, e) for s, e in zip(spans.starts, spans.ends)))
			else:
				return number
		except ValueError:
			return number
	return 0


if __name__ == "__main__":
	out = []
	bad = run(open(sys.argv[1], encoding="utf-8").read().split("\n"), out)
	sys.stdout.write("".join(line + "\n" for line in out))
	if bad:
		sys.stderr.write("error: line %d\n" % bad)
		sys.exit(1)
