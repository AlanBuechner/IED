"""The reference of the ledger task after the change: balance, list and report, with --account and --month on every
command. Prints what the program must print; exits as it must."""

import re
import sys

USAGE = "usage: ledger <file> <balance|list|report> [--account NAME] [--month YYYY-MM]"
MONTH = re.compile(r"^\d{4}-(0[1-9]|1[0-2])$")
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def cents_of(text):
	match = re.fullmatch(r"(-?)(\d+)(?:\.(\d{2}))?", text)
	if not match:
		return None
	value = int(match.group(2)) * 100 + int(match.group(3) or 0)
	return -value if match.group(1) else value


def money(cents):
	sign = "-" if cents < 0 else ""
	return "%s%d.%02d" % (sign, abs(cents) // 100, abs(cents) % 100)


def parse(text):
	rows = []
	for number, line in enumerate(text.split("\n"), 1):
		line = line.rstrip("\r")
		if number == 1 or not line:
			continue
		fields = line.split(",")
		if len(fields) != 5:
			return None, "line %d: expected 5 fields" % number
		if not DATE.match(fields[0]):
			return None, "line %d: bad date" % number
		if not fields[1] or not fields[2]:
			return None, "line %d: empty account or category" % number
		cents = cents_of(fields[3])
		if cents is None:
			return None, "line %d: bad amount" % number
		rows.append((fields[0], fields[1], fields[2], cents, fields[4]))
	return rows, None


def balance(rows):
	totals = {}
	for row in rows:
		totals[row[1]] = totals.get(row[1], 0) + row[3]
	return "".join("%s %s\n" % (account, money(totals[account])) for account in sorted(totals))


def listing(rows):
	return "".join("%s %s %s %s %s\n" % (r[0], r[1], r[2], money(r[3]), r[4]) for r in rows)


def report(rows):
	spent = {}
	for row in rows:
		if row[3] < 0:
			spent[row[2]] = spent.get(row[2], 0) - row[3]
	total = sum(spent.values())
	if not total:
		return "no spending\n"
	lines = []
	for category, amount in sorted(spent.items(), key=lambda item: (-item[1], item[0])):
		tenths = (amount * 2000 + total) // (2 * total)
		lines.append("%s %s %d.%d%%\n" % (category, money(amount), tenths // 10, tenths % 10))
	return "".join(lines) + "total %s\n" % money(total)


def main(argv):
	if len(argv) < 2 or argv[1] not in ("balance", "list", "report") or len(argv) % 2 != 0:
		print(USAGE, file=sys.stderr)
		return 2
	options = {}
	for i in range(2, len(argv), 2):
		if argv[i] not in ("--account", "--month"):
			print(USAGE, file=sys.stderr)
			return 2
		options[argv[i]] = argv[i + 1]
	if "--month" in options and not MONTH.match(options["--month"]):
		print("error: bad month %s" % options["--month"], file=sys.stderr)
		return 2
	try:
		with open(argv[0], encoding="utf-8", newline="") as f:
			text = f.read()
	except OSError:
		print("error: cannot read %s" % argv[0], file=sys.stderr)
		return 1
	rows, error = parse(text)
	if error:
		print("error: " + error, file=sys.stderr)
		return 1
	rows = [r for r in rows if r[1] == options.get("--account", r[1]) and r[0].startswith(options.get("--month", ""))]
	sys.stdout.write({"balance": balance, "list": listing, "report": report}[argv[1]](rows))
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
