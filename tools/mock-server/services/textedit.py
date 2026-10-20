"""Text edits on line/column ranges and their rebase over intervening edits (docs/10 document editing).

Positions are 0-based {line, col}; col counts UTF-16 code units so browser clients and the server agree.
Changes in one edit all refer to the document before the edit and must not overlap.
"""

from typing import NamedTuple


class Pos(NamedTuple):
	line: int
	col: int


class Change(NamedTuple):
	start: Pos
	end: Pos
	text: str


class BadEdit(Exception):
	pass


class Conflict(Exception):
	pass


def utf16_len(text):
	return len(text.encode("utf-16-le")) // 2


def parse_changes(raw):
	if not isinstance(raw, list):
		raise BadEdit("changes must be an array")
	return [_parse_change(item) for item in raw]


def _parse_change(item):
	try:
		rng, text = item["range"], item["text"]
		start, end = _parse_pos(rng["start"]), _parse_pos(rng["end"])
	except (KeyError, TypeError):
		raise BadEdit("a change is {range: {start: {line, col}, end: {line, col}}, text}")
	if not isinstance(text, str):
		raise BadEdit("change text must be a string")
	if end < start:
		raise BadEdit("range end is before start")
	return Change(start, end, text)


def _parse_pos(obj):
	line, col = obj["line"], obj["col"]
	if type(line) is not int or type(col) is not int or line < 0 or col < 0:
		raise BadEdit("line and col must be non-negative integers")
	return Pos(line, col)


def to_wire(changes):
	return [
		{"range": {"start": c.start._asdict(), "end": c.end._asdict()}, "text": c.text}
		for c in changes
	]


def end_position(text):
	lines = text.split("\n")
	return Pos(len(lines) - 1, utf16_len(lines[-1]))


def _after_insert(start, text):
	lines = text.split("\n")
	if len(lines) == 1:
		return Pos(start.line, start.col + utf16_len(text))
	return Pos(start.line + len(lines) - 1, utf16_len(lines[-1]))


def _shift(pos, change):
	after = _after_insert(change.start, change.text)
	if pos.line == change.end.line:
		return Pos(after.line, after.col + pos.col - change.end.col)
	return Pos(pos.line + after.line - change.end.line, pos.col)


def transform(change, applied):
	"""Moves `change` over an already-applied change; raises Conflict when their ranges overlap."""
	if applied.end <= change.start:
		return Change(_shift(change.start, applied), _shift(change.end, applied), change.text)
	if applied.start >= change.end:
		return change
	raise Conflict("ranges overlap")


def _sequential(changes):
	order = sorted(range(len(changes)), key=lambda i: (changes[i].start, changes[i].end, i), reverse=True)
	return [changes[i] for i in order]


def rebase(changes, history):
	"""Rebases changes made against an old version over the change sets applied since (oldest first)."""
	steps = [step for change_set in history for step in _sequential(change_set)]
	out = []
	for change in changes:
		for step in steps:
			change = transform(change, step)
		out.append(change)
	return out


def apply(text, changes):
	_check_disjoint(changes)
	starts = _line_starts(text)
	spans = sorted(
		((_index(text, starts, c.start), _index(text, starts, c.end), i, c.text) for i, c in enumerate(changes)),
		key=lambda span: (span[0], span[1], span[2]),
	)
	pieces, cursor = [], 0
	for begin, end, _, insert in spans:
		pieces.append(text[cursor:begin])
		pieces.append(insert)
		cursor = end
	pieces.append(text[cursor:])
	return "".join(pieces)


def _check_disjoint(changes):
	ordered = sorted(changes, key=lambda c: (c.start, c.end))
	for a, b in zip(ordered, ordered[1:]):
		if b.start < a.end:
			raise BadEdit("changes in one edit must not overlap")


def _line_starts(text):
	starts = [0]
	index = text.find("\n")
	while index >= 0:
		starts.append(index + 1)
		index = text.find("\n", index + 1)
	return starts


def _index(text, starts, pos):
	if pos.line >= len(starts):
		raise BadEdit("line %d is past the end of the document" % pos.line)
	begin = starts[pos.line]
	end = starts[pos.line + 1] - 1 if pos.line + 1 < len(starts) else len(text)
	units = 0
	for offset in range(begin, end + 1):
		if units == pos.col:
			return offset
		if units > pos.col or offset == end:
			break
		units += 2 if ord(text[offset]) > 0xFFFF else 1
	raise BadEdit("col %d is outside line %d or splits a character" % (pos.col, pos.line))
