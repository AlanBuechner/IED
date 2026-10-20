"""The primitive types a generated program uses, with their C4 and C spellings and value ranges."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Prim:
	c4: str
	c: str
	bits: int
	signed: bool
	floating: bool = False
	boolean: bool = False

	@property
	def integer(self):
		return not self.floating and not self.boolean

	@property
	def low(self):
		return -(1 << (self.bits - 1)) if self.signed else 0

	@property
	def high(self):
		return (1 << (self.bits - 1)) - 1 if self.signed else (1 << self.bits) - 1


INT8 = Prim('int8', 'int8_t', 8, True)
INT16 = Prim('int16', 'int16_t', 16, True)
INT32 = Prim('int32', 'int32_t', 32, True)
INT64 = Prim('int64', 'int64_t', 64, True)
UINT8 = Prim('uint8', 'uint8_t', 8, False)
UINT16 = Prim('uint16', 'uint16_t', 16, False)
UINT32 = Prim('uint32', 'uint32_t', 32, False)
UINT64 = Prim('uint64', 'uint64_t', 64, False)
FLOAT = Prim('float', 'float', 32, True, floating=True)
DOUBLE = Prim('double', 'double', 64, True, floating=True)
BOOL = Prim('bool', 'bool', 8, False, boolean=True)

INTEGERS = [INT8, INT16, INT32, INT64, UINT8, UINT16, UINT32, UINT64]
FLOATS = [FLOAT, DOUBLE]
ALL = INTEGERS + FLOATS + [BOOL]


def unsigned_of(t):
	"""The unsigned type of the same width as integer type `t`."""
	return {8: UINT8, 16: UINT16, 32: UINT32, 64: UINT64}[t.bits]


def promoted(t):
	"""C's integer promotion: anything narrower than int32 (and bool) becomes int32."""
	if t.boolean or (t.integer and t.bits < 32):
		return INT32
	return t


def common(a, b):
	"""C's usual arithmetic conversions for two operand types (C4 follows the same table)."""
	if a.floating or b.floating:
		return DOUBLE if DOUBLE in (a, b) else FLOAT
	a, b = promoted(a), promoted(b)
	if a == b:
		return a
	if a.bits != b.bits:
		return a if a.bits > b.bits else b
	return a if not a.signed else b
