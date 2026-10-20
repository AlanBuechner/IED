"""A random program without undefined behaviour, as one tree that renders to C4 and to C.

Rules that keep C defined (so any difference is a C4 bug): signed +, -, * run on the unsigned type of the same
width; division goes through helpers that reject zero and MIN / -1; shift amounts are masked to the promoted
width and signed left shifts run unsigned; floats reach integers through clamping helpers; loops are counted;
functions used in expressions are pure (they read globals but write only their locals), so C's unspecified
operand order cannot show.
"""

import random
from dataclasses import dataclass, field
from typing import List
from difftest import types as T
from difftest import helpers as H
from difftest.nodes import (Lit, Ref, Cast, Call, Binary, Unary, Conditional, Index, Member, CallMember, Deref,
	Declare, DeclarePointer, DeclareStruct, Jump, Assign, Run, If, For, While, DoWhile, Switch, Return)


@dataclass
class Variable:
	name: str
	type: T.Prim
	writable: bool = True


@dataclass
class ArrayVar:
	name: str
	type: T.Prim
	length: int


@dataclass
class StructType:
	name: str
	fields: List[Variable]

	# Spelled by its name in both languages (Lang.type reads these).
	@property
	def c4(self):
		return self.name

	@property
	def c(self):
		return self.name


@dataclass
class StructVar:
	name: str
	struct: StructType


@dataclass
class Function:
	name: str
	result: T.Prim
	params: List[Variable]
	body: list = field(default_factory=list)


@dataclass
class Program:
	structs: List[StructType] = field(default_factory=list)
	globals: List[Variable] = field(default_factory=list)
	global_values: dict = field(default_factory=dict)
	arrays: List[ArrayVar] = field(default_factory=list)
	array_values: dict = field(default_factory=dict)
	struct_vars: List[StructVar] = field(default_factory=list)
	struct_values: dict = field(default_factory=dict)
	functions: List[Function] = field(default_factory=list)
	procedures: List[Function] = field(default_factory=list)

	def readonly_globals(self):
		return [Variable(v.name, v.type, writable=False) for v in self.globals]


@dataclass
class Scope:
	variables: List[Variable]
	arrays: List[ArrayVar]
	structs: List[StructVar]
	functions: List[Function]
	writes_globals: bool
	pointers: List[Variable] = field(default_factory=list)
	in_loop: bool = False
	in_switch: bool = False

	def nested(self, loop=None, switch=None):
		return Scope(list(self.variables), list(self.arrays), list(self.structs), self.functions, self.writes_globals,
			list(self.pointers), self.in_loop if loop is None else loop, self.in_switch if switch is None else switch)

	def scalar_functions(self):
		return [f for f in self.functions if isinstance(f.result, T.Prim)]

	def struct_functions(self, struct=None):
		return [f for f in self.functions if isinstance(f.result, StructType) and (struct is None or f.result is struct)]


ARITHMETIC = ['+', '-', '*', '/', '%', '&', '|', '^', '<<', '>>']
COMPARISON = ['<', '>', '<=', '>=', '==', '!=']
NUMERIC = T.INTEGERS + T.FLOATS


class Generator:
	def __init__(self, seed, size=1.0):
		self.rng = random.Random(seed)
		self.size = size
		self.counter = 0

	# --- names and values ----------------------------------------------------------------------------------

	def name(self, prefix):
		self.counter += 1
		return '%s%d' % (prefix, self.counter)

	def value(self, t):
		r = self.rng
		if t.boolean:
			return r.random() < 0.5
		if t.floating:
			pick = r.random()
			if pick < 0.6:
				return r.randint(-64, 64) / 8.0
			if pick < 0.8:
				return float(r.randint(-1000000, 1000000))
			return r.choice([1e10, -1e10, 3.0e38, -3.0e38, 1e-5, 0.1, -0.3, 65504.0, 4294967296.0, 1e300 if t == T.DOUBLE else 1e30])
		pick = r.random()
		if pick < 0.3:
			return r.choice([t.low, t.high, 0, 1, t.high - 1, t.low + 1, -1 if t.signed else 2])
		if pick < 0.7:
			return r.randint(max(t.low, -100), min(t.high, 100))
		return r.randint(t.low, t.high)

	def pick_type(self, integers_only=False):
		return self.rng.choice(T.INTEGERS if integers_only else NUMERIC)

	# --- conversions ---------------------------------------------------------------------------------------

	def convert(self, e, t):
		if e.type == t:
			return e
		if e.type.floating and t.integer:
			return Call(t, H.float_to_int_name(t), [Cast(T.DOUBLE, e)])
		return Cast(t, e)

	# --- expressions ---------------------------------------------------------------------------------------

	def expr(self, t, depth, scope):
		r = self.rng.random()
		if depth <= 0 or r < 0.2:
			return self.leaf(t, scope)
		kinds = [self.arithmetic] * 5 + [self.compare] * 2 + [self.logic, self.unary, self.conditional,
			self.call, self.call, self.element, self.element, self.member, self.other_type, self.struct_call, self.pointee]
		return self.rng.choice(kinds)(t, depth - 1, scope)

	def leaf(self, t, scope):
		readable = [v for v in scope.variables]
		if readable and self.rng.random() < 0.65:
			v = self.rng.choice(readable)
			return self.convert(Ref(v.type, v.name), t)
		return Lit(t, self.value(t))

	def other_type(self, t, depth, scope):
		return self.convert(self.expr(self.rng.choice(T.ALL), depth, scope), t)

	def arithmetic(self, t, depth, scope):
		op = self.rng.choice(ARITHMETIC)
		integer_op = op in ('&', '|', '^', '<<', '>>', '%')
		a_type, b_type = self.pick_type(integer_op), self.pick_type(integer_op)
		a, b = self.expr(a_type, depth, scope), self.expr(b_type, depth, scope)
		common = T.common(a_type, b_type)
		if op in ('+', '-', '*'):
			if common.integer and common.signed:
				common = T.unsigned_of(common)
				a, b = Cast(common, a), Cast(common, b)
			node = Binary(common, op, a, b)
		elif op in ('/', '%'):
			if common.floating:
				node = Binary(common, '/', a, b)
			else:
				node = Call(common, H.division_name(op, common), [self.convert(a, common), self.convert(b, common)])
		elif op in ('&', '|', '^'):
			node = Binary(common, op, a, b)
		else:
			shifted = T.promoted(a_type)
			amount = Binary(T.INT32, '&', self.convert(b, T.INT32), Lit(T.INT32, shifted.bits - 1))
			if op == '<<' and shifted.signed:
				shifted = T.unsigned_of(shifted)
				a = Cast(shifted, a)
			node = Binary(shifted, op, a, amount)
		return self.convert(node, t)

	def compare(self, t, depth, scope):
		a_type, b_type = self.pick_type(), self.pick_type()
		node = Binary(T.INT32, self.rng.choice(COMPARISON), self.expr(a_type, depth, scope), self.expr(b_type, depth, scope))
		return Cast(t, node)

	def logic(self, t, depth, scope):
		op = self.rng.choice(['&&', '||'])
		node = Binary(T.INT32, op, self.expr(self.rng.choice(T.ALL), depth, scope), self.expr(self.rng.choice(T.ALL), depth, scope))
		return Cast(t, node)

	def unary(self, t, depth, scope):
		op = self.rng.choice(['-', '~', '!'])
		if op == '-':
			a_type = self.rng.choice(T.FLOATS + [T.UINT8, T.UINT16, T.UINT32, T.UINT64])
			result = a_type if a_type.floating else T.promoted(a_type)
		elif op == '~':
			a_type = self.pick_type(True)
			result = T.promoted(a_type)
		else:
			a_type = self.rng.choice(T.ALL)
			result = T.INT32
		node = Unary(result, op, self.expr(a_type, depth, scope))
		return Cast(t, node) if op == '!' else self.convert(node, t)

	def conditional(self, t, depth, scope):
		condition = self.convert(self.expr(self.rng.choice(T.ALL), depth, scope), T.BOOL)
		return Conditional(t, condition, self.expr(t, depth, scope), self.expr(t, depth, scope))

	def arguments(self, f, depth, scope):
		return [self.struct_value(p.type, depth, scope) if isinstance(p.type, StructType) else self.expr(p.type, depth, scope)
			for p in f.params]

	def call(self, t, depth, scope):
		functions = scope.scalar_functions()
		if not functions:
			return self.leaf(t, scope)
		f = self.rng.choice(functions)
		return self.convert(Call(f.result, f.name, self.arguments(f, depth, scope)), t)

	def struct_value(self, struct, depth, scope):
		"""A value of struct type: a variable of it, or a call that returns one."""
		variables = [s for s in scope.structs if s.struct is struct]
		makers = scope.struct_functions(struct)
		if makers and depth > 0 and (not variables or self.rng.random() < 0.4):
			f = self.rng.choice(makers)
			return Call(f.result, f.name, self.arguments(f, depth - 1, scope))
		if variables:
			s = self.rng.choice(variables)
			return Ref(struct, s.name)
		raise ValueError('no value of ' + struct.name)

	def struct_call(self, t, depth, scope):
		makers = scope.struct_functions()
		if not makers:
			return self.leaf(t, scope)
		f = self.rng.choice(makers)
		member = self.rng.choice(f.result.fields)
		return self.convert(CallMember(member.type, Call(f.result, f.name, self.arguments(f, depth, scope)), member.name), t)

	def pointee(self, t, depth, scope):
		if not scope.pointers:
			return self.leaf(t, scope)
		p = self.rng.choice(scope.pointers)
		return self.convert(Deref(p.type, p.name), t)

	def element(self, t, depth, scope):
		if not scope.arrays:
			return self.leaf(t, scope)
		a = self.rng.choice(scope.arrays)
		return self.convert(Index(a.type, a.name, a.length, self.expr(self.pick_type(True), depth, scope)), t)

	def member(self, t, depth, scope):
		if not scope.structs:
			return self.leaf(t, scope)
		s = self.rng.choice(scope.structs)
		f = self.rng.choice(s.struct.fields)
		return self.convert(Member(f.type, s.name, f.name), t)

	# --- statements ----------------------------------------------------------------------------------------

	def statements(self, count, depth, scope):
		body = []
		for _ in range(count):
			body.append(self.statement(depth, scope))
		return body

	def statement(self, depth, scope):
		kinds = [self.declare] * 3 + [self.assign] * 4 + [self.compound] * 2 + [self.store_element, self.store_member,
			self.point_at_local, self.store_through_pointer]
		if scope.in_loop or scope.in_switch:
			kinds.append(self.jump)
		if depth > 0:
			kinds += [self.branch, self.loop, self.while_loop, self.do_while, self.switch]
		return self.rng.choice(kinds)(depth, scope)

	def declare(self, depth, scope):
		t = self.rng.choice(T.ALL)
		v = Variable(self.name('l'), t)
		stmt = Declare(t, v.name, self.expr(t, self.expr_depth(), scope))
		scope.variables.append(v)
		return stmt

	def expr_depth(self):
		return self.rng.randint(1, max(1, int(4 * self.size)))

	def targets(self, scope):
		return [v for v in scope.variables if v.writable and (scope.writes_globals or not v.name.startswith('g'))]

	def assign(self, depth, scope):
		targets = self.targets(scope)
		if not targets:
			return self.declare(depth, scope)
		v = self.rng.choice(targets)
		return Assign(Ref(v.type, v.name), self.expr(v.type, self.expr_depth(), scope))

	def compound(self, depth, scope):
		targets = [v for v in self.targets(scope) if v.type.integer]
		if not targets:
			return self.assign(depth, scope)
		v = self.rng.choice(targets)
		ops = ['&=', '|=', '^=']
		if not v.type.signed:
			ops += ['+=', '-=']
			if v.type.bits in (8, 32, 64):
				ops.append('*=')
		return Assign(Ref(v.type, v.name), self.expr(v.type, self.expr_depth(), scope), self.rng.choice(ops))

	def store_element(self, depth, scope):
		arrays = [a for a in scope.arrays if scope.writes_globals or not a.name.startswith('g')]
		if not arrays:
			return self.assign(depth, scope)
		a = self.rng.choice(arrays)
		target = Index(a.type, a.name, a.length, self.expr(self.pick_type(True), 2, scope))
		return Assign(target, self.expr(a.type, self.expr_depth(), scope))

	def store_member(self, depth, scope):
		structs = [s for s in scope.structs if scope.writes_globals or not s.name.startswith('g')]
		if not structs:
			return self.assign(depth, scope)
		s = self.rng.choice(structs)
		f = self.rng.choice(s.struct.fields)
		return Assign(Member(f.type, s.name, f.name), self.expr(f.type, self.expr_depth(), scope))

	def point_at_local(self, depth, scope):
		locals_ = [v for v in scope.variables if v.writable and v.name.startswith('l')]
		if not locals_:
			return self.declare(depth, scope)
		v = self.rng.choice(locals_)
		p = Variable(self.name('q'), v.type)
		scope.pointers.append(p)
		return DeclarePointer(v.type, p.name, v.name)

	def store_through_pointer(self, depth, scope):
		if not scope.pointers:
			return self.assign(depth, scope)
		p = self.rng.choice(scope.pointers)
		return Assign(Deref(p.type, p.name), self.expr(p.type, self.expr_depth(), scope))

	def jump(self, depth, scope):
		keyword = 'continue' if scope.in_loop and (not scope.in_switch or self.rng.random() < 0.5) else 'break'
		if scope.in_switch and keyword == 'continue' and not scope.in_loop:
			keyword = 'break'
		condition = self.convert(self.expr(self.rng.choice(T.ALL), 2, scope), T.BOOL)
		return If(condition, [Jump(keyword)])

	def do_while(self, depth, scope):
		inner = scope.nested(loop=True, switch=False)
		counter = self.name('d')
		inner.variables.append(Variable(counter, T.INT32, writable=False))
		return DoWhile(counter, self.rng.randint(1, 4), self.statements(self.rng.randint(1, 3), depth - 1, inner))

	def branch(self, depth, scope):
		condition = self.convert(self.expr(self.rng.choice(T.ALL), 2, scope), T.BOOL)
		then = self.statements(self.rng.randint(1, 3), depth - 1, scope.nested())
		otherwise = self.statements(self.rng.randint(1, 3), depth - 1, scope.nested()) if self.rng.random() < 0.5 else None
		return If(condition, then, otherwise)

	def loop(self, depth, scope):
		inner = scope.nested(loop=True, switch=False)
		counter = self.name('i')
		inner.variables.append(Variable(counter, T.INT32, writable=False))
		return For(counter, self.rng.randint(1, 4), self.statements(self.rng.randint(1, 3), depth - 1, inner))

	def while_loop(self, depth, scope):
		inner = scope.nested(loop=True, switch=False)
		counter = self.name('w')
		body = While(counter, self.rng.randint(1, 4), [])
		inner.variables.append(Variable(counter, T.INT32, writable=False))
		body.body = self.statements(self.rng.randint(1, 3), depth - 1, inner)
		return body

	def switch(self, depth, scope):
		cases = [self.statements(self.rng.randint(1, 2), depth - 1, scope.nested(switch=True)) for _ in range(4)]
		default = self.statements(1, depth - 1, scope.nested(switch=True))
		return Switch(self.expr(self.pick_type(True), 2, scope), cases, default)

	# --- program -------------------------------------------------------------------------------------------

	def program(self):
		p = Program()
		for _ in range(self.rng.randint(1, 3)):
			fields = [Variable(self.name('f'), self.rng.choice(T.ALL)) for _ in range(self.rng.randint(1, 4))]
			p.structs.append(StructType(self.name('S'), fields))
		for _ in range(int(8 * self.size) + 2):
			v = Variable(self.name('g'), self.rng.choice(T.ALL))
			p.globals.append(v)
			p.global_values[v.name] = self.value(v.type)
		for _ in range(self.rng.randint(1, 3)):
			a = ArrayVar(self.name('ga'), self.rng.choice(T.ALL), self.rng.choice([2, 4, 8]))
			p.arrays.append(a)
			p.array_values[a.name] = [self.value(a.type) for _ in range(a.length)]
		for s in p.structs:
			sv = StructVar(self.name('gs'), s)
			p.struct_vars.append(sv)
			p.struct_values[sv.name] = [self.value(f.type) for f in s.fields]
		pure = []
		for _ in range(int(4 * self.size) + 2):
			pure.append(self.function(p, pure))
		p.functions = pure
		for _ in range(int(3 * self.size) + 1):
			f = Function(self.name('proc'), None, [])
			scope = Scope(list(p.globals), list(p.arrays), list(p.struct_vars), list(pure), True)
			f.body = self.statements(self.rng.randint(2, int(6 * self.size) + 2), 2, scope)
			p.procedures.append(f)
		return p

	def parameter_type(self, p, pure):
		# A struct parameter only when some value of it can be made: a global of it always exists.
		if self.rng.random() < 0.2:
			return self.rng.choice(p.structs)
		return self.rng.choice(T.ALL)

	def function(self, p, pure):
		params = [Variable(self.name('p'), self.parameter_type(p, pure)) for _ in range(self.rng.randint(0, 4))]
		returns_struct = self.rng.random() < 0.25
		f = Function(self.name('fn'), self.rng.choice(p.structs) if returns_struct else self.rng.choice(T.ALL), params)
		scalars = [v for v in params if isinstance(v.type, T.Prim)]
		struct_params = [StructVar(v.name, v.type) for v in params if isinstance(v.type, StructType)]
		scope = Scope(p.readonly_globals() + scalars, list(p.arrays), list(p.struct_vars) + struct_params, list(pure), False)
		f.body = self.statements(self.rng.randint(1, int(5 * self.size) + 1), 2, scope)
		if returns_struct:
			result = StructVar(self.name('r'), f.result)
			f.body.append(DeclareStruct(f.result, result.name, [self.expr(m.type, self.expr_depth(), scope) for m in f.result.fields]))
			scope.structs.append(result)
			f.body += self.statements(self.rng.randint(0, 2), 1, scope)
			f.body.append(Return(Ref(f.result, result.name)))
		else:
			f.body.append(Return(self.expr(f.result, self.expr_depth(), scope)))
		return f

	def calls(self, p):
		"""One call of each pure function, with arguments over the initial globals, for `main` to report."""
		scope = Scope(p.readonly_globals(), list(p.arrays), list(p.struct_vars), list(p.functions), False)
		out = []
		for f in p.functions:
			call = Call(f.result, f.name, self.arguments(f, 2, scope))
			if isinstance(f.result, StructType):
				out += [CallMember(m.type, call, m.name) for m in f.result.fields]
			else:
				out.append(call)
		return out
