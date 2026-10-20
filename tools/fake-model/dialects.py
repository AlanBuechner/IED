"""Tool calls rendered as plain text, the way models without (or with broken) native tool calling emit them (docs/12)."""

import json


def _args_json(arguments):
	return arguments if isinstance(arguments, str) else json.dumps(arguments)


def _call_json(name, arguments):
	args = json.loads(arguments) if isinstance(arguments, str) else arguments
	return json.dumps({"name": name, "arguments": args})


def hermes(name, arguments):
	return "<tool_call>\n%s\n</tool_call>" % _call_json(name, arguments)


def qwen(name, arguments):
	args = json.loads(arguments) if isinstance(arguments, str) else arguments
	params = "".join(
		"<parameter=%s>\n%s\n</parameter>\n" % (key, value if isinstance(value, str) else json.dumps(value))
		for key, value in args.items()
	)
	return "<tool_call>\n<function=%s>\n%s</function>\n</tool_call>" % (name, params)


def mistral(name, arguments):
	return "[TOOL_CALLS][%s]" % _call_json(name, arguments)


def mistral_args(name, arguments):
	return "[TOOL_CALLS]%s[ARGS]%s" % (name, _args_json(arguments))


def fenced_json(name, arguments):
	return "```json\n%s\n```" % _call_json(name, arguments)


def harmony(name, arguments):
	return "<|start|>assistant<|channel|>commentary to=functions.%s <|constrain|>json<|message|>%s<|call|>" % (name, _args_json(arguments))


DIALECTS = {
	"hermes": hermes,
	"qwen": qwen,
	"mistral": mistral,
	"mistral-args": mistral_args,
	"fenced-json": fenced_json,
	"harmony": harmony,
}


def render(dialect, name, arguments):
	if dialect not in DIALECTS:
		raise ValueError("unknown dialect %r (have: %s)" % (dialect, ", ".join(DIALECTS)))
	return DIALECTS[dialect](name, arguments)
