"""The reference of the jsonagg task: `python -I jsonagg_ref.py <orders.json> <report.json>` reads the orders, writes
the report, and fails as the program must (usage 2; an unreadable file or invalid input 1, with no report written)."""

import json
import sys

USAGE = "usage: jsonagg <orders.json> <report.json>"
STATUSES = ("paid", "cancelled", "refunded")
TOP_SKUS = 5


class InvalidInput(Exception):
	pass


def require(condition):
	if not condition:
		raise InvalidInput()


def is_integer(value):
	return isinstance(value, int) and not isinstance(value, bool)


def is_number(value):
	return isinstance(value, (int, float)) and not isinstance(value, bool)


def refuse_constant(name):
	raise InvalidInput()


def parse_item(item):
	require(isinstance(item, dict))
	sku, qty, price = item.get("sku"), item.get("qty"), item.get("price")
	require(isinstance(sku, str) and is_integer(qty) and qty > 0 and is_number(price))
	return sku, qty, round(price * 100)


def parse_order(order):
	require(isinstance(order, dict))
	customer, status, items = order.get("customer"), order.get("status"), order.get("items")
	require(isinstance(customer, str) and status in STATUSES and isinstance(items, list))
	return customer, status, [parse_item(item) for item in items]


def parse(text):
	"""[(customer, status, [(sku, qty, cents)])] of UTF-8 bytes; InvalidInput when they are not JSON of the orders' shape."""
	try:
		root = json.loads(text, parse_constant=refuse_constant)
	except ValueError:
		raise InvalidInput()
	require(isinstance(root, dict) and isinstance(root.get("orders"), list))
	return [parse_order(order) for order in root["orders"]]


def ranked(units):
	return sorted(units.items(), key=lambda entry: (-entry[1], entry[0]))


def aggregate(orders):
	customers, units = {}, {}
	totals = {"orders": 0, "paid": 0, "cancelled": 0, "refunded": 0, "revenue": 0}
	for name, status, items in orders:
		customer = customers.setdefault(name, {"orders": 0, "paid_orders": 0, "spent": 0, "units": {}})
		customer["orders"] += 1
		totals["orders"] += 1
		totals[status] += 1
		if status != "paid":
			continue
		customer["paid_orders"] += 1
		for sku, qty, cents in items:
			customer["spent"] += cents * qty
			totals["revenue"] += cents * qty
			customer["units"][sku] = customer["units"].get(sku, 0) + qty
			units[sku] = units.get(sku, 0) + qty
	return customers, totals, units


def dollars(cents):
	return cents / 100


def report(orders):
	customers, totals, units = aggregate(orders)
	return {
		"customers": {name: {"orders": c["orders"], "paid_orders": c["paid_orders"], "spent": dollars(c["spent"]),
			"favorite_sku": ranked(c["units"])[0][0] if c["units"] else None} for name, c in customers.items()},
		"totals": dict(totals, revenue=dollars(totals["revenue"])),
		"top_skus": [{"sku": sku, "qty": qty} for sku, qty in ranked(units)[:TOP_SKUS]],
	}


def main(argv):
	if len(argv) != 2:
		print(USAGE, file=sys.stderr)
		return 2
	try:
		with open(argv[0], "rb") as f:
			text = f.read()
	except OSError:
		print("error: cannot read %s" % argv[0], file=sys.stderr)
		return 1
	try:
		result = report(parse(text))
	except InvalidInput:
		print("error: invalid input", file=sys.stderr)
		return 1
	try:
		with open(argv[1], "w", encoding="utf-8", newline="\n") as f:
			json.dump(result, f, indent="\t", sort_keys=True)
			f.write("\n")
	except OSError:
		print("error: cannot write %s" % argv[1], file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
