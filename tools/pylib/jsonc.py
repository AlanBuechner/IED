"""JSONC loading: JSON plus // line comments and /* */ block comments (shared/host-api conventions)."""

import json


class JsoncError(ValueError):
	pass


def strip_comments(text):
	out = []
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		if c == '"':
			end = _string_end(text, i)
			out.append(text[i:end])
			i = end
		elif text.startswith("//", i):
			i = _line_end(text, i)
		elif text.startswith("/*", i):
			close = text.find("*/", i + 2)
			if close < 0:
				raise JsoncError("unterminated block comment at offset %d" % i)
			out.append(" ")
			i = close + 2
		else:
			out.append(c)
			i += 1
	return "".join(out)


def _string_end(text, start):
	i = start + 1
	while i < len(text):
		if text[i] == "\\":
			i += 2
		elif text[i] == '"':
			return i + 1
		else:
			i += 1
	raise JsoncError("unterminated string at offset %d" % start)


def _line_end(text, start):
	end = text.find("\n", start)
	return len(text) if end < 0 else end


def loads(text):
	return json.loads(strip_comments(text))


def load_file(path):
	with open(path, "r", encoding="utf-8") as f:
		return loads(f.read())
