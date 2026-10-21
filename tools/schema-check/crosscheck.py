"""Runs Bedrock's ValidateJson (the C4 program in c4/, `nuke build schemacheck`) and tools/pylib/minischema.py on the
same schemas and instances - the shared/host-api examples and 150 mutated copies of each - and compares the problem
paths each reports, so the two validators never drift apart.

	python -I tools/schema-check/crosscheck.py build/bin/Development-windows-x64/schemacheck/schemacheck.exe
"""

import copy
import json
import os
import random
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))
import jsonc  # noqa: E402
import minischema  # noqa: E402

EXE = sys.argv[1]
SCHEMAS = os.path.join(REPO, "shared", "host-api", "schemas")
EXAMPLES = os.path.join(REPO, "shared", "host-api", "examples")

PAIRS = [
	("project.v1.schema.json", os.path.join(EXAMPLES, "Game.ied.proj")),
	("solution.v1.schema.json", os.path.join(EXAMPLES, "MyGame.ied.sln")),
	("plugin-manifest.v1.schema.json", os.path.join(EXAMPLES, "markup-plugin", "ied-plugin.json")),
]


def c4_paths(schema, instance_path):
	out = subprocess.run([EXE, schema, instance_path], capture_output=True, text=True, timeout=60)
	if out.returncode != 0:
		return ("error", out.stdout.strip())
	return sorted(line.split("\t", 1)[0] for line in out.stdout.splitlines() if line)


def python_paths(registry, schema, instance):
	return sorted(p.path for p in minischema.Validator(registry, schema).validate(instance))


def mutations(doc, rng, count):
	"""Copies of `doc` with one leaf changed to a value of another type, one member dropped, or one added."""
	leaves = []

	def walk(node, path):
		if isinstance(node, dict):
			for k, v in node.items():
				leaves.append((path, k))
				walk(v, path + [k])
		elif isinstance(node, list):
			for i, v in enumerate(node):
				leaves.append((path, i))
				walk(v, path + [i])
	walk(doc, [])
	for _ in range(count):
		mutated = copy.deepcopy(doc)
		path, key = rng.choice(leaves)
		parent = mutated
		for step in path:
			parent = parent[step]
		action = rng.choice(["retype", "drop", "add"])
		if action == "drop" and isinstance(parent, dict):
			del parent[key]
		elif action == "add" and isinstance(parent, dict):
			parent["zz-extra-%d" % rng.randrange(100)] = rng.choice([1, "x", True, None, [], {}])
		else:
			parent[key] = rng.choice([7, 2.5, "text", True, None, [1, "a"], {"q": 1}])
		yield mutated


def main():
	registry = minischema.Registry()
	rng = random.Random(1234)
	compared = 0
	differences = 0
	work = tempfile.mkdtemp(prefix="schema-cross-")
	for schema_name, example in PAIRS:
		schema = os.path.join(SCHEMAS, schema_name)
		doc = jsonc.load_file(example)
		for i, instance in enumerate([doc] + list(mutations(doc, rng, 150))):
			path = os.path.join(work, "case-%s-%d.json" % (schema_name, i))
			with open(path, "w", encoding="utf-8") as f:
				json.dump(instance, f)
			mine = c4_paths(schema, path)
			theirs = python_paths(registry, schema, instance)
			compared += 1
			if mine != theirs:
				differences += 1
				if differences <= 8:
					print("DIFF", schema_name, i, "\n  c4:    ", mine, "\n  python:", theirs)
	print("compared %d instances, %d differences" % (compared, differences))
	return 1 if differences else 0


if __name__ == "__main__":
	sys.exit(main())
