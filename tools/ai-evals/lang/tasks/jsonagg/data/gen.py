"""The jsonagg benchmark: 200,000 orders of 20,000 customers over 5,000 skus (their popularity a Zipf law), 1 to 5 items
each, some with their members shuffled or unknown members added; writes <out>/input.json (about 45 MB). The report is
not compared (the bench compares standard output): it measures time and memory only."""

import json
import os
import random
import sys

ORDERS = 200000
CUSTOMERS = 20000
SKUS = 5000
STATUSES = ["paid"] * 16 + ["cancelled"] * 3 + ["refunded"]


def catalog(rng):
	"""[(sku, price in cents)]."""
	return [("%s-%04d" % (rng.choice("ABCDEFGHJK"), number), rng.choice([rng.randint(5, 2000), rng.randint(1, 500) * 100, rng.randint(2000, 50000)]))
		for number in range(SKUS)]


def price_value(cents):
	return cents // 100 if cents % 100 == 0 else cents / 100


def item(rng, sku, cents):
	return {"sku": sku, "qty": rng.randint(1, 4) if rng.random() < 0.9 else rng.randint(5, 40), "price": price_value(cents)}


def order(rng, number, customers, products, weights):
	items = [item(rng, sku, cents) for sku, cents in rng.choices(products, weights, k=rng.randint(1, 5))]
	members = [("id", number), ("customer", rng.choice(customers)), ("date", "2024-%02d-%02d" % (rng.randint(1, 12), rng.randint(1, 28))),
		("status", rng.choice(STATUSES)), ("items", items)]
	if rng.random() < 0.1:
		members.append(("note", {"text": "gift wrap", "items": len(items)}))
	if rng.random() < 0.2:
		rng.shuffle(members)
	return dict(members)


def main():
	out = sys.argv[1]
	rng = random.Random(9001)
	products = catalog(rng)
	weights = [1.0 / (rank + 1) for rank in range(len(products))]
	customers = ["customer-%05d" % number for number in range(CUSTOMERS)]
	with open(os.path.join(out, "input.json"), "w", encoding="utf-8", newline="\n") as f:
		f.write('{"orders": [\n')
		for number in range(1, ORDERS + 1):
			f.write(json.dumps(order(rng, number, customers, products, weights)))
			f.write(",\n" if number < ORDERS else "\n")
		f.write('], "generated": "2024-12-31"}\n')


if __name__ == "__main__":
	main()
