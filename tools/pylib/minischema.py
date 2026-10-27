"""A small JSON Schema (draft 2020-12 subset) validator for shared/host-api.

Supports exactly the keywords listed in KEYWORDS (and "x-" annotations, such as x-ied-tiers); any other
keyword in a schema is a SchemaError, so a schema can never silently rely on something this validator
ignores. Bedrock's Serialization::ValidateJson (C4, B68) implements the same dialect and checks. Regex patterns use
Python's re with re.ASCII (ECMA-262 \\d and \\w are ASCII-only).
"""

import math
import os
import re

import jsonc

ANNOTATIONS = {
	"$schema", "$id", "$defs", "definitions", "$comment", "title", "description", "default", "examples", "deprecated",
	"readOnly", "writeOnly", "format", "markdownDescription", "enumDescriptions",
}
ASSERTIONS = {
	"type", "enum", "const", "pattern", "minLength", "maxLength", "minimum", "maximum",
	"exclusiveMinimum", "exclusiveMaximum", "multipleOf", "required", "properties", "patternProperties",
	"additionalProperties", "propertyNames", "minProperties", "maxProperties", "dependentRequired", "dependentSchemas",
	"items", "prefixItems", "minItems", "maxItems", "uniqueItems", "contains", "minContains", "maxContains",
	"oneOf", "anyOf", "allOf", "not", "if", "then", "else", "$ref",
}
KEYWORDS = ANNOTATIONS | ASSERTIONS


class SchemaError(Exception):
	pass


class Problem:
	def __init__(self, path, message):
		self.path = path
		self.message = message

	def __str__(self):
		return "%s: %s" % (self.path or "/", self.message)


class Registry:
	"""Loads schema files by absolute path and resolves local and cross-file $refs."""

	def __init__(self):
		self._docs = {}
		self._patterns = {}

	def pattern(self, source):
		compiled = self._patterns.get(source)
		if compiled is None:
			compiled = self._patterns.setdefault(source, re.compile(source, re.ASCII))
		return compiled

	def load(self, path):
		path = os.path.normcase(os.path.abspath(path))
		if path not in self._docs:
			try:
				doc = jsonc.load_file(path)
			except (OSError, ValueError) as e:
				raise SchemaError("cannot load schema %s: %s" % (path, e))
			self._docs[path] = doc
			check_schema(doc, path)
		return self._docs[path]

	def resolve(self, ref, base_path):
		file_part, _, pointer = ref.partition("#")
		path = base_path
		if file_part:
			path = os.path.join(os.path.dirname(base_path), file_part)
		doc = self.load(path)
		return _follow_pointer(doc, pointer, ref), os.path.normcase(os.path.abspath(path))

	def verify_refs(self, path):
		"""Resolves every $ref reachable from a schema file; raises SchemaError on a dangling one."""
		base = os.path.normcase(os.path.abspath(path))
		seen = set()
		pending = [(self.load(base), base)]
		while pending:
			node, node_base = pending.pop()
			for ref in _refs_in(node):
				target, target_base = self.resolve(ref, node_base)
				key = (target_base, ref.partition("#")[2])
				if key not in seen:
					seen.add(key)
					pending.append((target, target_base))
		return len(seen)


def _refs_in(node):
	if isinstance(node, dict):
		for key, value in node.items():
			if key == "$ref":
				yield value
			elif key not in ("enum", "const", "default", "examples"):
				yield from _refs_in(value)
	elif isinstance(node, list):
		for item in node:
			yield from _refs_in(item)


def _follow_pointer(doc, pointer, ref):
	node = doc
	for raw in [p for p in pointer.split("/") if p]:
		part = raw.replace("~1", "/").replace("~0", "~")
		if isinstance(node, dict) and part in node:
			node = node[part]
		elif isinstance(node, list) and part.isdigit() and int(part) < len(node):
			node = node[int(part)]
		else:
			raise SchemaError("unresolvable $ref %r" % ref)
	return node


def check_schema(schema, where, pointer=""):
	"""Rejects unknown keywords anywhere in a schema document."""
	if isinstance(schema, bool):
		return
	if not isinstance(schema, dict):
		raise SchemaError("%s#%s: schema must be an object or boolean" % (where, pointer))
	for key, value in schema.items():
		if key not in KEYWORDS and not key.startswith("x-"):
			raise SchemaError("%s#%s: unsupported keyword %r" % (where, pointer, key))
		if key == "multipleOf" and (isinstance(value, bool) or not isinstance(value, (int, float)) or value <= 0):
			raise SchemaError("%s#%s: multipleOf is a number greater than 0" % (where, pointer))
		for sub_pointer, sub in _subschemas(key, value, pointer):
			check_schema(sub, where, sub_pointer)


def _subschemas(key, value, pointer):
	here = "%s/%s" % (pointer, key)
	if key in ("properties", "$defs", "definitions", "patternProperties", "dependentSchemas"):
		return [("%s/%s" % (here, k), v) for k, v in value.items()]
	if key in ("items", "additionalProperties", "not", "propertyNames", "contains", "if", "then", "else"):
		return [(here, value)]
	if key in ("oneOf", "anyOf", "allOf", "prefixItems"):
		return [("%s/%d" % (here, i), v) for i, v in enumerate(value)]
	return []


class Validator:
	def __init__(self, registry, schema_path, pointer=""):
		self.registry = registry
		self.base = os.path.normcase(os.path.abspath(schema_path))
		self.schema = _follow_pointer(registry.load(schema_path), pointer, pointer)

	def validate(self, instance):
		problems = []
		self._check(self.schema, self.base, instance, "", problems)
		return problems

	def is_valid(self, instance):
		return not self.validate(instance)

	def _check(self, schema, base, value, path, out):
		if schema is True:
			return
		if schema is False:
			out.append(Problem(path, "no value is allowed here"))
			return
		if "$ref" in schema:
			target, target_base = self.registry.resolve(schema["$ref"], base)
			self._check(target, target_base, value, path, out)
		for key, arg in schema.items():
			checker = _CHECKS.get(key)
			if checker:
				checker(self, schema, arg, base, value, path, out)

	def _branch_problems(self, sub, base, value, path):
		problems = []
		self._check(sub, base, value, path, problems)
		return problems


def json_type(value):
	if value is None:
		return "null"
	if isinstance(value, bool):
		return "boolean"
	if isinstance(value, int):
		return "integer"
	if isinstance(value, float):
		return "integer" if value.is_integer() else "number"
	if isinstance(value, str):
		return "string"
	if isinstance(value, list):
		return "array"
	if isinstance(value, dict):
		return "object"
	return type(value).__name__


def _type_matches(actual, wanted):
	return actual == wanted or (wanted == "number" and actual == "integer")


def json_equal(a, b):
	ta, tb = json_type(a), json_type(b)
	if {ta, tb} <= {"integer", "number"}:
		return a == b
	if ta != tb:
		return False
	if ta == "array":
		return len(a) == len(b) and all(json_equal(x, y) for x, y in zip(a, b))
	if ta == "object":
		return a.keys() == b.keys() and all(json_equal(a[k], b[k]) for k in a)
	return a == b


def _child(path, key):
	return "%s/%s" % (path, str(key).replace("~", "~0").replace("/", "~1"))


def _c_type(v, schema, arg, base, value, path, out):
	wanted = arg if isinstance(arg, list) else [arg]
	actual = json_type(value)
	if not any(_type_matches(actual, w) for w in wanted):
		out.append(Problem(path, "expected %s, got %s" % (" or ".join(wanted), actual)))


def _c_enum(v, schema, arg, base, value, path, out):
	if not any(json_equal(value, e) for e in arg):
		out.append(Problem(path, "%r is not one of %r" % (value, arg)))


def _c_const(v, schema, arg, base, value, path, out):
	if not json_equal(value, arg):
		out.append(Problem(path, "expected %r, got %r" % (arg, value)))


def _c_pattern(v, schema, arg, base, value, path, out):
	if isinstance(value, str) and not v.registry.pattern(arg).search(value):
		out.append(Problem(path, "%r does not match pattern %r" % (value, arg)))


def _c_min_length(v, schema, arg, base, value, path, out):
	if isinstance(value, str) and len(value) < arg:
		out.append(Problem(path, "string shorter than %d" % arg))


def _c_max_length(v, schema, arg, base, value, path, out):
	if isinstance(value, str) and len(value) > arg:
		out.append(Problem(path, "string longer than %d" % arg))


def _numeric(value):
	return json_type(value) in ("integer", "number")


def _c_minimum(v, schema, arg, base, value, path, out):
	if _numeric(value) and value < arg:
		out.append(Problem(path, "%r is less than minimum %r" % (value, arg)))


def _c_maximum(v, schema, arg, base, value, path, out):
	if _numeric(value) and value > arg:
		out.append(Problem(path, "%r is greater than maximum %r" % (value, arg)))


def _c_exclusive_minimum(v, schema, arg, base, value, path, out):
	if _numeric(value) and value <= arg:
		out.append(Problem(path, "%r is not greater than %r" % (value, arg)))


def _c_exclusive_maximum(v, schema, arg, base, value, path, out):
	if _numeric(value) and value >= arg:
		out.append(Problem(path, "%r is not less than %r" % (value, arg)))


# How far a quotient may be from a whole number and still count as one (0.3 / 0.1 is 2.9999999999999996).
MULTIPLE_TOLERANCE = 1e-9


def _is_multiple(value, of):
	if json_type(value) == "integer" and isinstance(value, int) and isinstance(of, int):
		return value % of == 0
	quotient = value / of
	return abs(quotient - math.floor(quotient + 0.5)) <= MULTIPLE_TOLERANCE * max(1.0, abs(quotient))


def _c_multiple_of(v, schema, arg, base, value, path, out):
	if _numeric(value) and not _is_multiple(value, arg):
		out.append(Problem(path, "%r is not a multiple of %r" % (value, arg)))


def _c_required(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name in arg:
			if name not in value:
				out.append(Problem(path, "missing required property %r" % name))


def _c_properties(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name, sub in arg.items():
			if name in value:
				v._check(sub, base, value[name], _child(path, name), out)


def _c_pattern_properties(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name in value:
			for pattern, sub in arg.items():
				if v.registry.pattern(pattern).search(name):
					v._check(sub, base, value[name], _child(path, name), out)


def _c_property_names(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name in value:
			v._check(arg, base, name, _child(path, name), out)


def _c_min_properties(v, schema, arg, base, value, path, out):
	if isinstance(value, dict) and len(value) < arg:
		out.append(Problem(path, "expected at least %d properties, got %d" % (arg, len(value))))


def _c_max_properties(v, schema, arg, base, value, path, out):
	if isinstance(value, dict) and len(value) > arg:
		out.append(Problem(path, "expected at most %d properties, got %d" % (arg, len(value))))


def _c_dependent_required(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name, needed in arg.items():
			if name in value:
				for other in needed:
					if other not in value:
						out.append(Problem(path, "property %r requires property %r" % (name, other)))


def _c_dependent_schemas(v, schema, arg, base, value, path, out):
	if isinstance(value, dict):
		for name, sub in arg.items():
			if name in value:
				v._check(sub, base, value, path, out)


def _c_additional(v, schema, arg, base, value, path, out):
	if not isinstance(value, dict):
		return
	declared = schema.get("properties", {})
	patterns = schema.get("patternProperties", {})
	for name in value:
		if name in declared or any(v.registry.pattern(p).search(name) for p in patterns):
			continue
		if arg is False:
			out.append(Problem(path, "unexpected property %r" % name))
		else:
			v._check(arg, base, value[name], _child(path, name), out)


def _c_items(v, schema, arg, base, value, path, out):
	if isinstance(value, list):
		for i in range(len(schema.get("prefixItems", [])), len(value)):
			v._check(arg, base, value[i], _child(path, i), out)


def _c_prefix_items(v, schema, arg, base, value, path, out):
	if isinstance(value, list):
		for i, (sub, item) in enumerate(zip(arg, value)):
			v._check(sub, base, item, _child(path, i), out)


def _c_contains(v, schema, arg, base, value, path, out):
	if not isinstance(value, list):
		return
	matching = sum(1 for i, item in enumerate(value) if not v._branch_problems(arg, base, item, _child(path, i)))
	least = schema.get("minContains", 1)
	most = schema.get("maxContains")
	if matching < least:
		out.append(Problem(path, "expected at least %d items that match 'contains', got %d" % (least, matching)))
	elif most is not None and matching > most:
		out.append(Problem(path, "expected at most %d items that match 'contains', got %d" % (most, matching)))


def _c_if(v, schema, arg, base, value, path, out):
	chosen = schema.get("then" if not v._branch_problems(arg, base, value, path) else "else")
	if chosen is not None:
		v._check(chosen, base, value, path, out)


def _c_min_items(v, schema, arg, base, value, path, out):
	if isinstance(value, list) and len(value) < arg:
		out.append(Problem(path, "expected at least %d items, got %d" % (arg, len(value))))


def _c_max_items(v, schema, arg, base, value, path, out):
	if isinstance(value, list) and len(value) > arg:
		out.append(Problem(path, "expected at most %d items, got %d" % (arg, len(value))))


def _c_unique_items(v, schema, arg, base, value, path, out):
	if arg and isinstance(value, list):
		for i in range(len(value)):
			for j in range(i):
				if json_equal(value[i], value[j]):
					out.append(Problem(path, "items %d and %d are equal" % (j, i)))
					return


def _c_one_of(v, schema, arg, base, value, path, out):
	results = [v._branch_problems(sub, base, value, path) for sub in arg]
	passing = [i for i, r in enumerate(results) if not r]
	if len(passing) == 1:
		return
	if len(passing) > 1:
		out.append(Problem(path, "matches %d oneOf branches %r, expected exactly one" % (len(passing), passing)))
		return
	best = _best_branch(v, arg, base, value, results)
	out.append(Problem(path, "matches no oneOf branch; closest is branch %d:" % best))
	out.extend(results[best])


def _best_branch(v, branches, base, value, results):
	"""Picks the branch whose const discriminator matches the value, else the one with fewest problems."""
	if isinstance(value, dict):
		for i, sub in enumerate(branches):
			resolved = sub
			if isinstance(sub, dict) and "$ref" in sub:
				resolved, _ = v.registry.resolve(sub["$ref"], base)
			props = resolved.get("properties", {}) if isinstance(resolved, dict) else {}
			for name, prop in props.items():
				if isinstance(prop, dict) and "const" in prop and name in value and json_equal(value[name], prop["const"]):
					return i
	return min(range(len(results)), key=lambda i: len(results[i]))


def _c_any_of(v, schema, arg, base, value, path, out):
	results = [v._branch_problems(sub, base, value, path) for sub in arg]
	if all(results):
		best = min(range(len(results)), key=lambda i: len(results[i]))
		out.append(Problem(path, "matches no anyOf branch; closest is branch %d:" % best))
		out.extend(results[best])


def _c_all_of(v, schema, arg, base, value, path, out):
	for sub in arg:
		v._check(sub, base, value, path, out)


def _c_not(v, schema, arg, base, value, path, out):
	if not v._branch_problems(arg, base, value, path):
		out.append(Problem(path, "value must not match the 'not' schema"))


_CHECKS = {
	"type": _c_type,
	"enum": _c_enum,
	"const": _c_const,
	"pattern": _c_pattern,
	"minLength": _c_min_length,
	"maxLength": _c_max_length,
	"minimum": _c_minimum,
	"maximum": _c_maximum,
	"exclusiveMinimum": _c_exclusive_minimum,
	"exclusiveMaximum": _c_exclusive_maximum,
	"multipleOf": _c_multiple_of,
	"required": _c_required,
	"properties": _c_properties,
	"patternProperties": _c_pattern_properties,
	"additionalProperties": _c_additional,
	"propertyNames": _c_property_names,
	"minProperties": _c_min_properties,
	"maxProperties": _c_max_properties,
	"dependentRequired": _c_dependent_required,
	"dependentSchemas": _c_dependent_schemas,
	"items": _c_items,
	"prefixItems": _c_prefix_items,
	"contains": _c_contains,
	"if": _c_if,
	"minItems": _c_min_items,
	"maxItems": _c_max_items,
	"uniqueItems": _c_unique_items,
	"oneOf": _c_one_of,
	"anyOf": _c_any_of,
	"allOf": _c_all_of,
	"not": _c_not,
}
