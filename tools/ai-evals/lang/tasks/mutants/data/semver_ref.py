"""The reference of the semver task's program: compare, satisfies and sort. Parses with the regular expression
semver.org gives; prints what the program must print and exits as it must.

	python semver_ref.py compare <a> <b> | satisfies <version> <range> | sort <file>
"""

import functools
import re
import sys

USAGE = "usage: semver compare <a> <b> | semver satisfies <version> <range> | semver sort <file>"
NUMBER = r"(?:0|[1-9][0-9]*)"
PRERELEASE_ID = r"(?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)"
VERSION = re.compile(r"(%s)\.(%s)\.(%s)(?:-(%s(?:\.%s)*))?(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?"
	% (NUMBER, NUMBER, NUMBER, PRERELEASE_ID, PRERELEASE_ID))
SPACES = re.compile(r"[ \t\n\r\v\f]+")
INT64_MAX = 2 ** 63 - 1


def parse_version(text):
	"""(major, minor, patch, prerelease, build) or None."""
	match = VERSION.fullmatch(text)
	if not match:
		return None
	core = [int(match.group(i)) for i in (1, 2, 3)]
	if max(core) > INT64_MAX:
		return None
	prerelease = match.group(4).split(".") if match.group(4) is not None else []
	build = match.group(5).split(".") if match.group(5) is not None else []
	return (core[0], core[1], core[2], prerelease, build)


def compare_identifier(a, b):
	if a.isdigit() and b.isdigit():
		return (int(a) > int(b)) - (int(a) < int(b))
	if a.isdigit():
		return -1
	if b.isdigit():
		return 1
	return (a > b) - (a < b)


def compare(a, b):
	for x, y in zip(a[:3], b[:3]):
		if x != y:
			return -1 if x < y else 1
	pa, pb = a[3], b[3]
	if not pa or not pb:
		return (len(pb) > len(pa)) - (len(pb) < len(pa))
	for x, y in zip(pa, pb):
		order = compare_identifier(x, y)
		if order:
			return order
	return (len(pa) > len(pb)) - (len(pa) < len(pb))


def upper(op, v):
	"""The first version ^v or ~v excludes."""
	major, minor, patch = v[:3]
	if op == "~":
		return (major, minor + 1, 0, [], [])
	if major > 0:
		return (major + 1, 0, 0, [], [])
	if minor > 0:
		return (0, minor + 1, 0, [], [])
	return (0, 0, patch + 1, [], [])


def parse_range(text):
	"""[(op, version)] with ^ and ~ expanded, or None."""
	comparators = []
	for field in SPACES.split(text.strip(" \t\n\r\v\f")):
		if not field:
			continue
		op = next((o for o in (">=", "<=", ">", "<", "=", "^", "~") if field.startswith(o)), "")
		v = parse_version(field[len(op):])
		if v is None:
			return None
		if op in ("^", "~"):
			comparators += [(">=", v), ("<", upper(op, v))]
		else:
			comparators.append((op or "=", v))
	return comparators or None


def holds(op, bound, v):
	order = compare(v, bound)
	return {"=": order == 0, ">": order > 0, ">=": order >= 0, "<": order < 0, "<=": order <= 0}[op]


def satisfies(v, comparators):
	if not all(holds(op, bound, v) for op, bound in comparators):
		return False
	return not v[3] or any(bound[3] and bound[:3] == v[:3] for _, bound in comparators)


def text_of(v):
	return "%d.%d.%d" % v[:3] + ("-" + ".".join(v[3]) if v[3] else "") + ("+" + ".".join(v[4]) if v[4] else "")


def fail(message):
	sys.stderr.write("error: %s\n" % message)
	return 1


def main(args):
	if len(args) == 3 and args[0] == "compare":
		a, b = parse_version(args[1]), parse_version(args[2])
		if a is None or b is None:
			return fail("invalid version " + (args[1] if a is None else args[2]))
		print(compare(a, b))
	elif len(args) == 3 and args[0] == "satisfies":
		v = parse_version(args[1])
		if v is None:
			return fail("invalid version " + args[1])
		comparators = parse_range(args[2])
		if comparators is None:
			return fail("invalid range " + args[2])
		print("yes" if satisfies(v, comparators) else "no")
	elif len(args) == 2 and args[0] == "sort":
		try:
			with open(args[1], "rb") as f:
				text = f.read().decode("utf-8")
		except OSError:
			return fail("cannot read " + args[1])
		versions = []
		for line in text.split("\n"):
			line = line.strip(" \t\n\r\v\f")
			if not line:
				continue
			v = parse_version(line)
			if v is None:
				return fail("invalid version " + line)
			versions.append(v)
		for v in sorted(versions, key=functools.cmp_to_key(compare)):
			print(text_of(v))
	else:
		sys.stderr.write(USAGE + "\n")
		return 2
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
