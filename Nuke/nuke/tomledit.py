"""Editing a manifest's text in place: one key of one table set or removed, every comment and line around it kept, so a
command that changes a manifest leaves the rest of it as its owner wrote it. Values are strings and lists of strings."""

import re

KEY_LINE = re.compile(r'^[ \t]*("(?:[^"\\]|\\.)*"|[A-Za-z0-9_-]+)[ \t]*=')
TABLE_LINE = re.compile(r"^[ \t]*\[")


def render(value):
	if isinstance(value, str):
		return _string(value)
	return "[" + ", ".join(_string(item) for item in value) + "]"


def _string(text):
	return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _lines(text):
	"""(start, end) offsets of each line, its line break included."""
	spans = []
	start = 0
	while start < len(text):
		end = text.find("\n", start)
		end = len(text) if end < 0 else end + 1
		spans.append((start, end))
		start = end
	return spans


def _header(line):
	"""The name of the table a header line opens ("workspace", "project.driver"), else None."""
	stripped = line.split("#", 1)[0].strip()
	if stripped.startswith("[[") and stripped.endswith("]]"):
		return stripped[2:-2].strip()
	if stripped.startswith("[") and stripped.endswith("]"):
		return stripped[1:-1].strip()
	return None


def _table_lines(text, table):
	"""The indexes into _lines(text) of the table's own lines (after its header, before the next one); None when the
	text has no such table. The text before the first header is the table ""."""
	spans = _lines(text)
	inside = table == ""
	found = inside
	chosen = []
	for index, (start, end) in enumerate(spans):
		name = _header(text[start:end]) if TABLE_LINE.match(text[start:end]) else None
		if name is not None:
			inside = name == table
			found = found or inside
			continue
		if inside:
			chosen.append(index)
	return (chosen, spans) if found else (None, spans)


def _skip_string(text, i):
	"""Just past the string whose quote is at `i`."""
	quote = text[i]
	i += 1
	while i < len(text) and text[i] != quote and text[i] != "\n":
		i += 2 if quote == '"' and text[i] == "\\" else 1
	return min(i + 1, len(text))


def _value_end(text, at):
	"""Just past the value starting at `at`: its closing bracket for an array (strings and comments within skipped), its
	closing quote for a string, else the end of the word; a comment after it is not part of it."""
	if at < len(text) and text[at] in "\"'":
		return _skip_string(text, at)
	if at >= len(text) or text[at] != "[":
		end = at
		while end < len(text) and text[end] not in "#\n":
			end += 1
		return len(text[:end].rstrip())
	depth = 0
	i = at
	while i < len(text):
		c = text[i]
		if c in "\"'":
			i = _skip_string(text, i)
			continue
		if c == "#":
			while i < len(text) and text[i] != "\n":
				i += 1
			continue
		if c == "[":
			depth += 1
		elif c == "]":
			depth -= 1
			if depth == 0:
				return i + 1
		i += 1
	return i


def _key_name(token):
	return token[1:-1] if token.startswith('"') else token


def _find(text, table, key):
	"""(line start, value start, value end) of `key` in `table`, or None."""
	indexes, spans = _table_lines(text, table)
	if indexes is None:
		return None
	for index in indexes:
		start, end = spans[index]
		match = KEY_LINE.match(text[start:end])
		if match and _key_name(match.group(1)) == key:
			value = start + match.end()
			while value < len(text) and text[value] in " \t":
				value += 1
			return start, value, _value_end(text, value)
	return None


def has_table(text, table):
	return _table_lines(text, table)[0] is not None


def set_key(text, table, key, value):
	"""`text` with `key` of `table` set to `value`: replaced in place, else added after the table's last key (a new
	table at the end when there is none)."""
	rendered = render(value)
	found = _find(text, table, key)
	if found is not None:
		_, start, end = found
		return text[:start] + rendered + text[end:]
	indexes, spans = _table_lines(text, table)
	line = f"{key} = {rendered}\n"
	if indexes is None:
		gap = "" if not text or text.endswith("\n\n") else ("\n" if text.endswith("\n") else "\n\n")
		return f"{text}{gap}[{table}]\n{line}"
	keyed = [i for i in indexes if KEY_LINE.match(text[spans[i][0]:spans[i][1]])]
	if keyed:
		_, start, end = _find(text, table, _key_name(KEY_LINE.match(text[spans[keyed[-1]][0]:spans[keyed[-1]][1]]).group(1)))
		at = text.find("\n", end)
		at = len(text) if at < 0 else at + 1
	else:
		at = spans[indexes[0]][0] if indexes else len(text)
	prefix = "" if at == 0 or text[at - 1] == "\n" else "\n"
	return text[:at] + prefix + line + text[at:]


def remove_key(text, table, key):
	"""`text` without `key` of `table` (its whole line, or lines for a multi-line array); unchanged when absent."""
	found = _find(text, table, key)
	if found is None:
		return text
	line, _, end = found
	after = text.find("\n", end)
	after = len(text) if after < 0 else after + 1
	return text[:line] + text[after:]


def add_table(text, table, keys):
	"""`text` with a new table `table` holding `keys` (name -> value) put first, before every other line but the leading
	comments."""
	body = "".join(f"{k} = {render(v)}\n" for k, v in keys.items())
	spans = _lines(text)
	at = 0
	for start, end in spans:
		if text[start:end].lstrip().startswith("#"):
			at = end
			continue
		break
	return text[:at] + f"[{table}]\n{body}" + ("\n" if at < len(text) else "") + text[at:]
