"""The reference of the optimize task: what `logstats <file>` prints for an access log, and the log generator."""

import math
import random
import sys

CLASSES = (("2xx", 200, 300), ("4xx", 400, 500), ("5xx", 500, 600))
TOP_PATHS = 10
TOP_ERROR_IPS = 5


def parse(line):
	words = line.split(" ")
	if len(words) != 7:
		return None
	try:
		status, latency, size = int(words[4]), int(words[5]), int(words[6])
	except ValueError:
		return None
	if status < 100 or status > 599 or latency < 0 or size < 0:
		return None
	return words[1], words[3], status, latency, size


def percentile(sorted_values, percent):
	rank = max(1, math.ceil(percent * len(sorted_values) / 100))
	return sorted_values[rank - 1]


def report(text):
	requests = skipped = total_bytes = 0
	ips, paths, errors = set(), {}, {}
	latencies = {name: [] for name, _, _ in CLASSES}
	for line in text.split("\n"):
		line = line.rstrip("\r")
		if not line:
			continue
		parsed = parse(line)
		if parsed is None:
			skipped += 1
			continue
		ip, path, status, latency, size = parsed
		requests += 1
		total_bytes += size
		ips.add(ip)
		paths[path] = paths.get(path, 0) + 1
		for name, low, high in CLASSES:
			if low <= status < high:
				latencies[name].append(latency)
		if status >= 500:
			errors[ip] = errors.get(ip, 0) + 1
	out = ["requests %d" % requests, "skipped %d" % skipped, "unique_ips %d" % len(ips), "bytes %d" % total_bytes, "top_paths"]
	out += ["%s %d" % item for item in sorted(paths.items(), key=lambda kv: (-kv[1], kv[0]))[:TOP_PATHS]]
	for name, _, _ in CLASSES:
		values = sorted(latencies[name])
		out.append("latency_%s %s" % (name, " ".join(str(percentile(values, p)) for p in (50, 95, 99)) if values else "none"))
	out.append("top_error_ips")
	out += ["%s %d" % item for item in sorted(errors.items(), key=lambda kv: (-kv[1], kv[0]))[:TOP_ERROR_IPS]]
	return "\n".join(out) + "\n"


def generate(path, lines, seed):
	rng = random.Random(seed)
	ips = ["10.%d.%d.%d" % (rng.randrange(256), rng.randrange(256), rng.randrange(1, 255)) for _ in range(6000)]
	paths = ["/api/%s/%d" % (rng.choice(["items", "users", "orders", "search", "cart", "auth", "reports"]), i) for i in range(400)]
	ip_weights = [1.0 / (i + 1) ** 0.7 for i in range(len(ips))]
	path_weights = [1.0 / (i + 1) for i in range(len(paths))]
	methods = ["GET"] * 8 + ["POST", "PUT"]
	statuses = [200] * 80 + [201] * 5 + [204] * 3 + [301, 304] + [400] * 3 + [401, 403] + [404] * 3 + [500] * 2 + [502, 503]
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		chunk = []
		picked_ips = rng.choices(ips, ip_weights, k=lines)
		picked_paths = rng.choices(paths, path_weights, k=lines)
		for i in range(lines):
			second = i // 50
			stamp = "2024-05-%02dT%02d:%02d:%02dZ" % (1 + second // 86400 % 28, second // 3600 % 24, second // 60 % 60, second % 60)
			latency = int(rng.lognormvariate(3.5, 0.8))
			chunk.append("%s %s %s %s %d %d %d\n" % (stamp, picked_ips[i], rng.choice(methods), picked_paths[i], rng.choice(statuses), latency, rng.randrange(200, 90000)))
			if len(chunk) >= 10000:
				f.write("".join(chunk))
				chunk = []
		f.write("".join(chunk))


if __name__ == "__main__":
	if sys.argv[1] == "generate":
		generate(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
	else:
		with open(sys.argv[1], encoding="utf-8") as f:
			sys.stdout.write(report(f.read()))
