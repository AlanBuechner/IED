"""The optimize benchmark: a 1,000,000-line access log in <out>/input.log and the reference report in <out>/expected."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logstats_ref  # noqa: E402


def main():
	out = sys.argv[1]
	log = os.path.join(out, "input.log")
	logstats_ref.generate(log, 1000000, 2024)
	with open(log, encoding="utf-8") as f:
		report = logstats_ref.report(f.read())
	with open(os.path.join(out, "expected"), "w", encoding="utf-8", newline="\n") as f:
		f.write(report)


if __name__ == "__main__":
	main()
