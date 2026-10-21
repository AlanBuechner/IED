import copy
import json
import os
import pathlib
import sys
import tempfile
import unittest

PYLIB = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PYLIB))

import jsonc  # noqa: E402
import minischema  # noqa: E402

HOST_API = PYLIB.parents[1] / "shared" / "host-api"


def validator_for(schema, folder, name="s.json", extra=None):
	for file_name, doc in [(name, schema)] + list((extra or {}).items()):
		with open(os.path.join(folder, file_name), "w", encoding="utf-8") as f:
			json.dump(doc, f)
	return minischema.Validator(minischema.Registry(), os.path.join(folder, name))


class KeywordTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.addCleanup(self.tmp.cleanup)

	def problems(self, schema, instance, extra=None):
		return [str(p) for p in validator_for(schema, self.tmp.name, extra=extra).validate(instance)]

	def test_type_distinguishes_bool_from_integer(self):
		self.assertTrue(self.problems({"type": "integer"}, True))
		self.assertFalse(self.problems({"type": "integer"}, 3))
		self.assertFalse(self.problems({"type": "integer"}, 3.0))
		self.assertTrue(self.problems({"type": "integer"}, 3.5))
		self.assertFalse(self.problems({"type": "number"}, 3))
		self.assertFalse(self.problems({"type": ["string", "null"]}, None))

	def test_enum_and_const_use_json_equality(self):
		self.assertTrue(self.problems({"enum": [1, "a"]}, True))
		self.assertFalse(self.problems({"enum": [1, "a"]}, 1.0))
		self.assertTrue(self.problems({"const": {"a": [1]}}, {"a": [2]}))
		self.assertFalse(self.problems({"const": {"a": [1]}}, {"a": [1]}))

	def test_pattern_is_ascii_and_searches(self):
		schema = {"type": "string", "pattern": "^\\d+$"}
		self.assertFalse(self.problems(schema, "123"))
		self.assertTrue(self.problems(schema, "\u0661\u0662"))
		self.assertFalse(self.problems({"pattern": "b"}, "abc"))
		self.assertFalse(self.problems({"pattern": "b"}, 7))

	def test_object_keywords(self):
		schema = {
			"type": "object",
			"required": ["a"],
			"properties": {"a": {"type": "string"}},
			"additionalProperties": False,
		}
		self.assertFalse(self.problems(schema, {"a": "x"}))
		self.assertIn("/: missing required property 'a'", self.problems(schema, {}))
		self.assertIn("/: unexpected property 'b'", self.problems(schema, {"a": "x", "b": 1}))
		self.assertIn("/a: expected string, got integer", self.problems(schema, {"a": 1}))
		typed = {"additionalProperties": {"type": "integer"}}
		self.assertIn("/k~1x: expected integer, got string", self.problems(typed, {"k/x": "s"}))

	def test_array_keywords(self):
		schema = {"items": {"type": "string"}, "minItems": 1, "maxItems": 2, "uniqueItems": True}
		self.assertFalse(self.problems(schema, ["a"]))
		self.assertTrue(self.problems(schema, []))
		self.assertTrue(self.problems(schema, ["a", "b", "c"]))
		self.assertTrue(self.problems(schema, ["a", "a"]))
		self.assertIn("/1: expected string, got integer", self.problems(schema, ["a", 1]))

	def test_numeric_and_string_bounds(self):
		self.assertTrue(self.problems({"minimum": 0}, -1))
		self.assertFalse(self.problems({"minimum": 0}, 0))
		self.assertTrue(self.problems({"maximum": 5}, 6))
		self.assertTrue(self.problems({"exclusiveMinimum": 0}, 0))
		self.assertTrue(self.problems({"exclusiveMaximum": 5}, 5))
		self.assertTrue(self.problems({"minLength": 2}, "a"))
		self.assertTrue(self.problems({"maxLength": 1}, "ab"))

	def test_combinators(self):
		one_of = {"oneOf": [{"type": "string"}, {"type": "array"}]}
		self.assertFalse(self.problems(one_of, "s"))
		self.assertTrue(self.problems(one_of, 1))
		both = {"oneOf": [{"required": ["a"]}, {"required": ["b"]}]}
		self.assertTrue(any("exactly one" in p for p in self.problems(both, {"a": 1, "b": 2})))
		self.assertFalse(self.problems({"anyOf": [{"type": "string"}, {"minimum": 3}]}, 4))
		self.assertTrue(self.problems({"allOf": [{"type": "integer"}, {"minimum": 3}]}, 2))
		self.assertTrue(self.problems({"not": {"type": "string"}}, "s"))

	def test_one_of_reports_discriminated_branch(self):
		schema = {"oneOf": [
			{"properties": {"t": {"const": "a"}, "x": {"type": "string"}}, "required": ["t", "x"]},
			{"properties": {"t": {"const": "b"}}, "required": ["t", "y", "z"]},
		]}
		found = self.problems(schema, {"t": "b"})
		self.assertIn("/: matches no oneOf branch; closest is branch 1:", found)
		self.assertIn("/: missing required property 'y'", found)

	def test_local_and_cross_file_refs(self):
		other = {"$defs": {"name": {"type": "string", "pattern": "^[a-z]+$"}}}
		schema = {
			"$defs": {"local": {"$ref": "other.json#/$defs/name"}},
			"properties": {"n": {"$ref": "#/$defs/local"}},
		}
		self.assertFalse(self.problems(schema, {"n": "abc"}, extra={"other.json": other}))
		self.assertTrue(self.problems(schema, {"n": "ABC"}, extra={"other.json": other}))

	def test_unknown_keyword_is_a_schema_error(self):
		with self.assertRaises(minischema.SchemaError):
			validator_for({"properties": {"a": {"patternProperties": {}}}}, self.tmp.name)

	def test_annotations_and_x_keywords_are_accepted(self):
		schema = {"properties": {"a": {"type": "integer", "format": "int32", "x-ied-tiers": ["app"], "x-ied-merge": "replace"}}}
		self.assertFalse(self.problems(schema, {"a": 1}))
		self.assertTrue(self.problems(schema, {"a": "1"}))

	def test_dangling_ref_is_a_schema_error(self):
		path = os.path.join(self.tmp.name, "s.json")
		with open(path, "w", encoding="utf-8") as f:
			json.dump({"properties": {"a": {"$ref": "#/$defs/missing"}}}, f)
		with self.assertRaises(minischema.SchemaError):
			minischema.Registry().verify_refs(path)


class HostApiTests(unittest.TestCase):
	"""The real schemas reject broken copies of the real examples, so the examples check is not vacuous."""

	def setUp(self):
		self.registry = minischema.Registry()

	def validator(self, rel):
		return minischema.Validator(self.registry, HOST_API / rel)

	def test_examples_valid(self):
		project = jsonc.load_file(HOST_API / "examples" / "Game.ied.proj")
		self.assertEqual([], self.validator("schemas/project.v1.schema.json").validate(project))

	def test_broken_project_rejected(self):
		project = jsonc.load_file(HOST_API / "examples" / "Game.ied.proj")
		broken = copy.deepcopy(project)
		del broken["version"]
		self.assertTrue(self.validator("schemas/project.v1.schema.json").validate(broken))
		broken = copy.deepcopy(project)
		broken["tasks"]["build"]["steps"][0]["task"] = "also"
		self.assertTrue(self.validator("schemas/project.v1.schema.json").validate(broken))

	def test_broken_manifest_rejected_through_cross_file_ref(self):
		manifest = jsonc.load_file(HOST_API / "examples" / "markup-plugin" / "ied-plugin.json")
		validator = self.validator("schemas/plugin-manifest.v1.schema.json")
		self.assertEqual([], validator.validate(manifest))
		broken = copy.deepcopy(manifest)
		broken["id"] = "Bad Id"
		self.assertTrue(validator.validate(broken))

	def test_frames(self):
		frames = self.validator("protocol/frames.v1.schema.json")
		self.assertTrue(frames.is_valid({"t": "evt", "topic": "chat:42", "seq": 1, "kind": "x"}))
		self.assertFalse(frames.is_valid({"t": "evt", "topic": "chat:42", "seq": 0, "kind": "x"}))
		self.assertFalse(frames.is_valid({"t": "evt", "topic": "Bad Topic", "seq": 1, "kind": "x"}))
		self.assertFalse(frames.is_valid({"t": "res", "id": 1, "ok": False, "error": {"code": "nope", "message": ""}}))
		self.assertFalse(frames.is_valid({"t": "nope"}))


class JsoncTests(unittest.TestCase):
	def test_comments_stripped_outside_strings(self):
		text = '{ // c\n "a": "//not", /* b */ "b": "/*x*/", "c": "q\\"//" }'
		self.assertEqual({"a": "//not", "b": "/*x*/", "c": 'q"//'}, jsonc.loads(text))

	def test_unterminated(self):
		with self.assertRaises(ValueError):
			jsonc.loads('{"a": 1 /* x')


if __name__ == "__main__":
	unittest.main()
