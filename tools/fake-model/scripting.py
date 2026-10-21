"""Script files: loading, validation, {placeholder} substitution. Format: tools/fake-model/README.md."""

import json
import pathlib

OPS = {"headers", "role", "text", "reasoning", "toolCall", "textToolCall", "stall", "cut", "malformed", "raw", "finish", "usage", "done", "error", "progress"}
OPTIONS = {"chunk", "delayMs", "field", "keepalive"}
SCRIPT_KEYS = {"description", "stopClass", "harness", "responses"}
RESPONSE_KEYS = {"steps", "wire", "headers", "chunkDelayMs", "note"}
WIRE_KEYS = {
	"transport", "status", "finishReason", "text", "textIncludes", "reasoningIncludes", "toolCalls",
	"malformed", "minSilence", "maxSilence", "comments", "errorIncludes",
}
TRANSPORTS = {"done", "eof", "reset", "http_error", "no_response"}
PLACEHOLDERS = ("n_ctx", "prompt_tokens", "request", "model")


class ScriptError(ValueError):
	pass


def validate(script, name):
	_keys(script, SCRIPT_KEYS, name)
	for key in ("description", "stopClass", "harness"):
		if not isinstance(script.get(key), str):
			raise ScriptError("%s: %r must be a string" % (name, key))
	responses = script.get("responses")
	if not isinstance(responses, list) or not responses:
		raise ScriptError("%s: 'responses' must be a non-empty array" % name)
	for i, response in enumerate(responses):
		where = "%s: responses[%d]" % (name, i)
		_keys(response, RESPONSE_KEYS, where)
		if response.get("headers", "immediate") not in ("immediate", "lazy"):
			raise ScriptError("%s: headers must be 'immediate' or 'lazy'" % where)
		_keys(response.get("wire", {}), WIRE_KEYS, where + ".wire")
		if response.get("wire", {}).get("transport", "done") not in TRANSPORTS:
			raise ScriptError("%s.wire: transport must be one of %s" % (where, sorted(TRANSPORTS)))
		for j, step in enumerate(response.get("steps", [])):
			_check_step(step, "%s.steps[%d]" % (where, j))
	return script


def _keys(obj, allowed, where):
	if not isinstance(obj, dict):
		raise ScriptError("%s must be an object" % where)
	unknown = set(obj) - allowed
	if unknown:
		raise ScriptError("%s: unknown keys %s" % (where, sorted(unknown)))


def _check_step(step, where):
	_keys(step, OPS | OPTIONS, where)
	ops = [k for k in step if k in OPS]
	if len(ops) != 1:
		raise ScriptError("%s: a step needs exactly one of %s" % (where, sorted(OPS)))
	op = ops[0]
	if op == "cut" and step["cut"] not in ("now", "before-headers"):
		raise ScriptError("%s: cut must be 'now' or 'before-headers'" % where)
	if op in ("toolCall", "textToolCall") and not {"name", "arguments"} <= set(step[op]):
		raise ScriptError("%s: %s needs name and arguments" % (where, op))
	if op == "error" and not isinstance(step["error"].get("status"), int):
		raise ScriptError("%s: error needs an integer status" % where)


def substitute(value, values):
	"""Replaces {placeholders}; a string that is exactly one placeholder takes the value's own type (e.g. an int)."""
	if isinstance(value, str):
		whole = value[1:-1] if value.startswith("{") and value.endswith("}") else None
		if whole in PLACEHOLDERS and whole in values:
			return values[whole]
		for key in PLACEHOLDERS:
			value = value.replace("{%s}" % key, str(values.get(key, "{%s}" % key)))
		return value
	if isinstance(value, list):
		return [substitute(v, values) for v in value]
	if isinstance(value, dict):
		return {k: substitute(v, values) for k, v in value.items()}
	return value


class Library:
	def __init__(self, folder):
		self.folder = pathlib.Path(folder)
		self.scripts = {}
		for path in sorted(self.folder.glob("*.json")):
			with open(path, "r", encoding="utf-8") as f:
				try:
					self.scripts[path.stem] = validate(json.load(f), path.name)
				except ValueError as e:
					raise ScriptError("%s: %s" % (path.name, e))

	def get(self, name):
		if name not in self.scripts:
			raise KeyError("no script %r (have: %s)" % (name, ", ".join(sorted(self.scripts))))
		return self.scripts[name]
