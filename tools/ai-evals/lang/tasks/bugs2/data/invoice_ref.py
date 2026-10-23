"""The reference of the bugs task: invoice totals as README.md's rules say (the fixed program)."""

import sys

BULK_QUANTITY, BULK_OFF, GOLD_OFF, TAX = 10, 10, 5, 8


def percent_of(cents, percent):
	return (cents * percent + 50) // 100


def line_cents(quantity, unit):
	amount = quantity * unit
	return amount - percent_of(amount, BULK_OFF) if quantity >= BULK_QUANTITY else amount


def cents_of(text):
	units, _, fraction = text.partition(".")
	return int(units) * 100 + int(fraction or 0)


def money(cents):
	return "%d.%02d" % (cents // 100, cents % 100)


def main(path):
	try:
		text = open(path, encoding="utf-8").read()
	except OSError:
		print("error: cannot read %s" % path, file=sys.stderr)
		return 1
	orders = {}
	for number, line in enumerate(text.split("\n"), 1):
		line = line.rstrip("\r")
		if number == 1 or not line:
			continue
		order, customer, tier, sku, quantity, price = line.split(",")
		entry = orders.setdefault(order, {"customer": customer, "tier": tier, "lines": []})
		entry["lines"].append((int(quantity), cents_of(price)))
	out, total = [], 0
	for order, entry in orders.items():
		subtotal = sum(line_cents(q, p) for q, p in entry["lines"])
		gold = percent_of(subtotal, GOLD_OFF) if entry["tier"] == "gold" else 0
		tax = percent_of(subtotal - gold, TAX)
		order_total = subtotal - gold + tax
		total += order_total
		out.append("%s %s %s" % (order, entry["customer"], money(order_total)))
	count = len(orders)
	average = (total + count // 2) // count if count else 0
	out.append("orders %d total %s" % (count, money(total)))
	out.append("average %s" % money(average))
	sys.stdout.write("\n".join(out) + "\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1]))
