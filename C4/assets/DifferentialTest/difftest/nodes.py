"""Expression and statement trees of a generated program, rendered as C4 or as C.

Each node renders through `Lang`, which knows only spelling differences (type names, array declarators,
struct declarations); the trees themselves are the same program in both languages.
"""

from dataclasses import dataclass, field
from typing import List, Optional
from difftest import types as T


class Lang:
	def __init__(self, c4):
		self.c4 = c4

	def type(self, t):
		return t.c4 if self.c4 else t.c

	def decl(self, t, name, length=None):
		"""`T name` or an array of `length` elements (`T[n] name` in C4, `T name[n]` in C)."""
		if length is None:
			return '%s %s' % (self.type(t), name)
		return '%s[%d] %s' % (self.type(t), length, name) if self.c4 else '%s %s[%d]' % (self.type(t), name, length)

	def struct_type(self, name):
		return name


def literal(t, value):
	"""A literal of type `t` spelled the same in both languages."""
	if t.boolean:
		return 'true' if value else 'false'
	if t.floating:
		text = repr(float(value))
		if 'e' not in text and '.' not in text and 'n' not in text:
			text += '.0'
		return text + ('f' if t == T.FLOAT else '')
	if t.signed and value == t.low:
		return '(%s - 1%s)' % (str(value + 1), 'LL' if t.bits == 64 else '')
	suffix = ''
	if t.bits == 64:
		suffix = 'LL' if t.signed else 'ULL'
	elif not t.signed and t.bits == 32:
		suffix = 'U'
	text = str(value) + suffix
	return '(%s)' % text if value < 0 else text


# --- expressions ------------------------------------------------------------------------------------------

@dataclass
class Expr:
	type: T.Prim

	def render(self, lang):
		raise NotImplementedError


@dataclass
class Lit(Expr):
	value: object = 0

	def render(self, lang):
		return literal(self.type, self.value)


@dataclass
class Ref(Expr):
	name: str = ''

	def render(self, lang):
		return self.name


@dataclass
class Cast(Expr):
	operand: Expr = None

	def render(self, lang):
		return '(%s)(%s)' % (lang.type(self.type), self.operand.render(lang))


@dataclass
class Call(Expr):
	function: str = ''
	args: List[Expr] = field(default_factory=list)

	def render(self, lang):
		return '%s(%s)' % (self.function, ', '.join(a.render(lang) for a in self.args))


@dataclass
class Binary(Expr):
	op: str = '+'
	left: Expr = None
	right: Expr = None

	def render(self, lang):
		return '(%s %s %s)' % (self.left.render(lang), self.op, self.right.render(lang))


@dataclass
class Unary(Expr):
	op: str = '-'
	operand: Expr = None

	def render(self, lang):
		return '(%s(%s))' % (self.op, self.operand.render(lang))


@dataclass
class Conditional(Expr):
	condition: Expr = None
	then: Expr = None
	otherwise: Expr = None

	def render(self, lang):
		return '(%s ? %s : %s)' % (self.condition.render(lang), self.then.render(lang), self.otherwise.render(lang))


@dataclass
class Index(Expr):
	array: str = ''
	length: int = 1
	index: Expr = None

	def render(self, lang):
		return '%s[(%s) & %dU]' % (self.array, Cast(T.UINT32, self.index).render(lang), self.length - 1)


@dataclass
class Member(Expr):
	object: str = ''
	field: str = ''

	def render(self, lang):
		return '%s.%s' % (self.object, self.field)


@dataclass
class CallMember(Expr):
	"""A field of a struct a call returns: `f(args).x`."""
	call: Expr = None
	field: str = ''

	def render(self, lang):
		return '%s.%s' % (self.call.render(lang), self.field)


@dataclass
class Deref(Expr):
	pointer: str = ''

	def render(self, lang):
		return '(*%s)' % self.pointer


# --- statements -------------------------------------------------------------------------------------------

class Stmt:
	def lines(self, lang, indent):
		raise NotImplementedError


@dataclass
class Declare(Stmt):
	type: T.Prim
	name: str
	value: Expr

	def lines(self, lang, indent):
		return [indent + '%s = %s;' % (lang.decl(self.type, self.name), self.value.render(lang))]


@dataclass
class DeclarePointer(Stmt):
	type: T.Prim
	name: str
	target: str

	def lines(self, lang, indent):
		return [indent + '%s* %s = &%s;' % (lang.type(self.type), self.name, self.target)]


@dataclass
class DeclareStruct(Stmt):
	struct: object
	name: str
	values: List[Expr]

	def lines(self, lang, indent):
		return [indent + '%s %s = { %s };' % (self.struct.name, self.name, ', '.join(v.render(lang) for v in self.values))]


@dataclass
class Jump(Stmt):
	keyword: str

	def lines(self, lang, indent):
		return [indent + self.keyword + ';']


@dataclass
class Assign(Stmt):
	target: Expr
	value: Expr
	op: str = '='

	def lines(self, lang, indent):
		return [indent + '%s %s %s;' % (self.target.render(lang), self.op, self.value.render(lang))]


@dataclass
class Run(Stmt):
	call: Expr

	def lines(self, lang, indent):
		return [indent + self.call.render(lang) + ';']


@dataclass
class If(Stmt):
	condition: Expr
	then: List[Stmt]
	otherwise: Optional[List[Stmt]] = None

	def lines(self, lang, indent):
		out = [indent + 'if (%s) {' % self.condition.render(lang)]
		out += block(self.then, lang, indent + '\t')
		if self.otherwise:
			out.append(indent + '} else {')
			out += block(self.otherwise, lang, indent + '\t')
		return out + [indent + '}']


@dataclass
class For(Stmt):
	counter: str
	count: int
	body: List[Stmt]

	def lines(self, lang, indent):
		head = 'for (%s = 0; %s < %d; %s++) {' % (lang.decl(T.INT32, self.counter), self.counter, self.count, self.counter)
		return [indent + head] + block(self.body, lang, indent + '\t') + [indent + '}']


@dataclass
class While(Stmt):
	counter: str
	count: int
	body: List[Stmt]

	def lines(self, lang, indent):
		out = [indent + '%s = %d;' % (lang.decl(T.INT32, self.counter), self.count)]
		out.append(indent + 'while (%s > 0) {' % self.counter)
		out.append(indent + '\t%s--;' % self.counter)
		return out + block(self.body, lang, indent + '\t') + [indent + '}']


@dataclass
class DoWhile(Stmt):
	counter: str
	count: int
	body: List[Stmt]

	def lines(self, lang, indent):
		out = [indent + '%s = %d;' % (lang.decl(T.INT32, self.counter), self.count), indent + 'do {']
		out.append(indent + '	%s--;' % self.counter)
		return out + block(self.body, lang, indent + '	') + [indent + '} while (%s > 0);' % self.counter]


@dataclass
class Switch(Stmt):
	selector: Expr
	cases: List[List[Stmt]]
	default: List[Stmt]

	def lines(self, lang, indent):
		out = [indent + 'switch ((%s) & %d) {' % (Cast(T.INT32, self.selector).render(lang), len(self.cases) - 1)]
		for i, body in enumerate(self.cases):
			out.append(indent + 'case %d: {' % i)
			out += block(body, lang, indent + '\t')
			out.append(indent + '\tbreak;')
			out.append(indent + '}')
		out.append(indent + 'default: {')
		out += block(self.default, lang, indent + '\t')
		out.append(indent + '\tbreak;')
		out.append(indent + '}')
		return out + [indent + '}']


@dataclass
class Return(Stmt):
	value: Expr

	def lines(self, lang, indent):
		return [indent + 'return %s;' % self.value.render(lang)]


def block(stmts, lang, indent):
	out = []
	for s in stmts:
		out += s.lines(lang, indent)
	return out
