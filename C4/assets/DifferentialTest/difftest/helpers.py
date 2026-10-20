"""Helper functions every generated program carries, written once per language: division without undefined
behaviour, float-to-integer conversion that clamps, and the bits of a number for the checksum."""

from difftest import types as T
from difftest.nodes import Lang, literal


def division_name(op, t):
	return '%s_%s' % ('div' if op == '/' else 'mod', t.c4)


def float_to_int_name(t):
	return 'f2i_%s' % t.c4


def _division(lang, op, t):
	# The integer types C's promotions produce: int32, int64, uint32, uint64.
	ty = lang.type(t)
	guard = '(b == 0)'
	if t.signed:
		guard = '(b == 0 || (a == %s && b == (-1)))' % literal(t, t.low)
	return [
		'%s %s(%s a, %s b) {' % (ty, division_name(op, t), ty, ty),
		'\tif %s return a;' % guard,
		'\treturn a %s b;' % op,
		'}',
	]


def _float_to_int(lang, t):
	# Converting a double outside the target's range is undefined in C: clamp first (NaN becomes 0).
	ty = lang.type(t)
	if t.bits == 64:
		upper = '18446744073709551616.0' if not t.signed else '9223372036854775808.0'
	else:
		upper = repr(float(t.high + 1))
	lower = repr(float(t.low - 1)) if t.signed else '-1.0'
	if t.bits == 64 and t.signed:
		lower_test = 'v < -9223372036854775808.0'
	else:
		lower_test = 'v <= %s' % lower
	return [
		'%s %s(double v) {' % (ty, float_to_int_name(t)),
		'\tif (v != v) return 0;',
		'\tif (v >= %s) return %s;' % (upper, literal(t, t.high)),
		'\tif (%s) return %s;' % (lower_test, literal(t, t.low)),
		'\treturn (%s)(v);' % ty,
		'}',
	]


def _bits(lang):
	u64, u32 = lang.type(T.UINT64), lang.type(T.UINT32)
	return [
		'%s bits_double(double x) {' % u64,
		'\tif (x != x) return 9221120237041090560ULL;',
		'\t%s b = 0ULL;' % u64,
		'\tmemcpy(&b, &x, 8);',
		'\treturn b;',
		'}',
		'%s bits_float(float x) {' % u64,
		'\tif (x != x) return 2143289344ULL;',
		'\t%s b = 0U;' % u32,
		'\tmemcpy(&b, &x, 4);',
		'\treturn (%s)(b);' % u64,
		'}',
		'void report(%s line, %s value) {' % (lang.type(T.INT32), u64),
		'\tprintf("%d %llu\\n", line, value);',
		'}',
	]


def source(lang):
	lines = []
	for t in (T.INT32, T.INT64, T.UINT32, T.UINT64):
		for op in ('/', '%'):
			lines += _division(lang, op, t)
	for t in T.INTEGERS:
		lines += _float_to_int(lang, t)
	lines += _bits(lang)
	return lines


def preamble(lang):
	if lang.c4:
		return []
	return ['#include <stdint.h>', '#include <stdbool.h>', '#include <stdio.h>', '#include <string.h>', '']


C4 = Lang(True)
C = Lang(False)
