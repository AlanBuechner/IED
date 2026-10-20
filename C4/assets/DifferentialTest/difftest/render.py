"""A generated program as C4 or C source: declarations, helpers, functions, then a `main` that reports every
global, array element and struct field (and each pure function's result) after each procedure runs."""

from difftest import types as T
from difftest import helpers as H
from difftest.nodes import Lang, Ref, Call, Index, Member, Lit, literal, block


def _struct(lang, s):
	fields = ['\t%s;' % lang.decl(f.type, f.name) for f in s.fields]
	if lang.c4:
		return ['struct %s {' % s.name] + fields + ['}']
	return ['typedef struct %s {' % s.name] + fields + ['} %s;' % s.name]


def _globals(lang, p):
	lines = []
	for v in p.globals:
		lines.append('%s = %s;' % (lang.decl(v.type, v.name), literal(v.type, p.global_values[v.name])))
	for a in p.arrays:
		values = ', '.join(literal(a.type, x) for x in p.array_values[a.name])
		lines.append('%s = { %s };' % (lang.decl(a.type, a.name, a.length), values))
	for s in p.struct_vars:
		values = ', '.join(literal(f.type, x) for f, x in zip(s.struct.fields, p.struct_values[s.name]))
		lines.append('%s %s = { %s };' % (s.struct.name, s.name, values))
	return lines


def _function(lang, f):
	params = ', '.join(lang.decl(v.type, v.name) for v in f.params)
	result = lang.type(f.result) if f.result else 'void'
	return ['%s %s(%s) {' % (result, f.name, params)] + block(f.body, lang, '\t') + ['}']


def _bits(lang, expr):
	"""The checksum word of a value: an integer widened to uint64, a float's bits (NaN made one pattern)."""
	if expr.type == T.FLOAT:
		return 'bits_float(%s)' % expr.render(lang)
	if expr.type == T.DOUBLE:
		return 'bits_double(%s)' % expr.render(lang)
	return '(%s)(%s)' % (lang.type(T.UINT64), expr.render(lang))


def _state(p):
	values = [Ref(v.type, v.name) for v in p.globals]
	for a in p.arrays:
		values += [Index(a.type, a.name, a.length, Lit(T.INT32, i)) for i in range(a.length)]
	for s in p.struct_vars:
		values += [Member(f.type, s.name, f.name) for f in s.struct.fields]
	return values


def _main(lang, p, calls):
	lines = ['%s main() {' % lang.type(T.INT32)]
	line = 0
	state = _state(p)

	def report_all():
		nonlocal line
		for value in state:
			lines.append('\treport(%d, %s);' % (line, _bits(lang, value)))
			line += 1

	report_all()
	for call in calls:
		lines.append('\treport(%d, %s);' % (line, _bits(lang, call)))
		line += 1
	for proc in p.procedures:
		lines.append('\t%s();' % proc.name)
		report_all()
	lines += ['\treturn 0;', '}']
	return lines


def source(lang, p, calls):
	lines = list(H.preamble(lang))
	for s in p.structs:
		lines += _struct(lang, s)
	lines += _globals(lang, p)
	lines += H.source(lang)
	for f in p.functions + p.procedures:
		lines += _function(lang, f)
	lines += _main(lang, p, calls)
	return '\n'.join(lines) + '\n'
