"""Validates IED files against the shared/host-api schemas.

Usage:
	python -I tools/schema-check/schema_check.py                 # schemas + shared/host-api examples + fixtures + the shipped plugins
	python -I tools/schema-check/schema_check.py <path>...       # files or folders (scanned recursively)
	python -I tools/schema-check/schema_check.py --schema S F... # force schema S for files F

Files are matched to schemas by name (see RULES). *.frames.jsonl holds one WebSocket frame per line.
Exit code: 0 when everything is valid, 1 on any problem, 2 on a usage or schema error.
"""

import argparse
import fnmatch
import os
import pathlib
import sys

TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / "pylib"))

import jsonc  # noqa: E402
import minischema  # noqa: E402

REPO = TOOLS.parent
HOST_API = REPO / "shared" / "host-api"
SCHEMAS = {
	"solution": HOST_API / "schemas" / "solution.v1.schema.json",
	"project": HOST_API / "schemas" / "project.v1.schema.json",
	"federation": HOST_API / "schemas" / "federation.v1.schema.json",
	"plugin-manifest": HOST_API / "schemas" / "plugin-manifest.v1.schema.json",
	"agent-mode": HOST_API / "schemas" / "agent-mode.v1.schema.json",
	"frames": HOST_API / "protocol" / "frames.v1.schema.json",
}
RULES = [
	("*.iedsln", "solution"),
	("*.iedproj", "project"),
	("*.iedfed", "federation"),
	("*.ied.sln", "solution"),   # the names before (docs/03 "Files"), still read
	("*.ied.proj", "project"),
	("*.ied.fed", "federation"),
	("ied-plugin.json", "plugin-manifest"),
	("*.frames.jsonl", "frames"),
]
SKIP_DIRS = {".git", "build", "bin-int", "third_party", "node_modules", "__pycache__", "local"}
# The plugins IED and the C4 toolchain ship: a manifest the schema refuses fails to load in every workspace (BUG-385:
# the C4 plugin's section maxTokens over the schema's 600 left every C4 workspace without its language server).
DEFAULT_TARGETS = [HOST_API / "examples", TOOLS / "schema-check" / "fixtures", REPO / "plugins", REPO / "C4" / "IedPlugin"]


def schema_for(path):
	name = os.path.basename(path)
	for pattern, kind in RULES:
		if fnmatch.fnmatch(name, pattern):
			return kind
	parts = pathlib.PurePath(path).parts
	if name.endswith(".json") and len(parts) >= 3 and parts[-3] == ".ied" and parts[-2] == "modes":
		return "agent-mode"
	return None


def discover(target):
	target = pathlib.Path(target)
	if target.is_file():
		return [target]
	found = []
	for root, dirs, files in os.walk(target):
		dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
		found.extend(pathlib.Path(root, f) for f in sorted(files) if schema_for(os.path.join(root, f)))
	return found


def check_schemas(registry):
	for kind, path in SCHEMAS.items():
		registry.load(path)
		registry.verify_refs(path)


def validate_file(registry, path, kind):
	schema = SCHEMAS.get(kind, kind)
	validator = minischema.Validator(registry, schema)
	if str(path).endswith(".jsonl"):
		return _validate_lines(validator, path)
	try:
		instance = jsonc.load_file(path)
	except (OSError, ValueError) as e:
		return ["cannot parse: %s" % e]
	return [str(p) for p in validator.validate(instance)]


def _validate_lines(validator, path):
	problems = []
	with open(path, "r", encoding="utf-8") as f:
		for number, line in enumerate(f, 1):
			if not line.strip() or line.lstrip().startswith("//"):
				continue
			try:
				instance = jsonc.loads(line)
			except ValueError as e:
				problems.append("line %d: cannot parse: %s" % (number, e))
				continue
			problems.extend("line %d: %s" % (number, p) for p in validator.validate(instance))
	return problems


def run(targets, forced_schema, out):
	registry = minischema.Registry()
	check_schemas(registry)
	files = [(f, forced_schema or schema_for(f)) for t in targets for f in discover(t)]
	failures = 0
	for path, kind in files:
		problems = validate_file(registry, path, kind)
		status = "FAIL" if problems else "ok"
		out.write("%-4s %s (%s)\n" % (status, _display(path), kind))
		for p in problems:
			out.write("     %s\n" % p)
		failures += bool(problems)
	out.write("%d schema(s) checked, %d file(s) validated, %d failed\n" % (len(SCHEMAS), len(files), failures))
	return 1 if failures else 0


def _display(path):
	try:
		return pathlib.Path(path).resolve().relative_to(REPO).as_posix()
	except ValueError:
		return str(path)


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("targets", nargs="*", help="files or folders to validate")
	parser.add_argument("--schema", help="schema kind (%s) or schema file path" % ", ".join(SCHEMAS))
	args = parser.parse_args(argv)
	forced = args.schema if args.schema in SCHEMAS or not args.schema else str(pathlib.Path(args.schema).resolve())
	try:
		return run(args.targets or DEFAULT_TARGETS, forced, sys.stdout)
	except minischema.SchemaError as e:
		sys.stderr.write("schema error: %s\n" % e)
		return 2


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
