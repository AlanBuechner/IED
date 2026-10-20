# 16 - Inline assembly

C4 code can drop to machine instructions with an `asm` block: inside a function it is a statement that reads
and writes C4 variables through named operands; at file scope it adds assembly to the program as it is. The
design follows Rust's `asm!`: Intel syntax by default, operands named in the template, explicit directions
and register classes, and conservative defaults (an `asm` block may read and write memory and change the flags
unless its options say otherwise). It lowers one-to-one to LLVM inline assembly.

See also: [09 - Statements](09-statements-and-control-flow.md), [11 - Vectors](11-vectors-and-swizzling.md) (the
`simd_*` intrinsics cover most vector work without assembly), [14 - Builtins](14-builtins.md).

---

## 1. An `asm` statement

**Status:** **Implemented**

```c4
uint64 ReadTimestamp() {
	uint64 low;
	uint64 high;
	asm(nomem, nostack) {
		"rdtsc";
		out("eax") low;
		out("edx") high;
	}
	return (high << 32) | low;
}

int32 AddTwice(int32 a, int32 b) {
	int32 sum = a;
	asm(pure, nomem, nostack) {
		"add {sum}, {b}";
		"add {sum}, {b}";
		inout(reg) sum;
		in(reg) b;
	}
	return sum;
}
```

An `asm` statement is the keyword, an optional option list in parentheses, and a block. The block holds, in any
order:

| Item | Meaning |
|---|---|
| `"text";` | one line of the template, ended by `;` (adjacent strings join into one line, as everywhere; the last item may omit its `;`) |
| `in(class) x;` | the value of the variable `x` goes in; the template calls it `{x}` |
| `in(class) name = expr;` | the value of any expression goes in, called `{name}` |
| `out(class) x;` / `out(class) name = location;` | the operand's final value is stored into `x` (or the location) |
| `inout(class) x;` | `x` goes in and its new value comes back out, in one register |
| `const name = expr;` | a constant (folded at compile time) written into the template as a number |
| `clobber("rcx", "memory", ...);` | registers (or `"memory"`, `"flags"`) the block changes without an operand saying so |
| `clobber_abi("C");` | every register a C call may change (the Windows x64 volatile registers) |

### 1.1 Register classes

| Class | Holds | Values |
|---|---|---|
| `reg` | a general-purpose register (`rax`, ..., `r15`) | integers, `bool`, pointers, enums |
| `xmm` / `ymm` | an SSE or AVX register | `float`, `double`, packed vectors of 16 or 32 bytes (and pure packed vector objects) |
| `zmm` | an AVX-512 register (`xmm0`-`xmm31` with AVX-512) | 64-byte packed vectors, and any `xmm` value |

`ymm` and `zmm` values need a target with AVX or AVX-512 (`-march`, [11](11-vectors-and-swizzling.md#8-the-simd-target)).
| `"rax"`, `"eax"`, `"xmm3"`, ... | that register | a value of the register's kind |

A value of the wrong kind for its class is an error that names the class to use (`a 'float' goes in an xmm
register: write in(xmm) f`). A `bool` goes in as 0 or 1 and comes out as `true` when the register is non-zero.

### 1.2 The template

- `{name}` is an operand: the register the compiler chose, sized for the operand's type (a 32-bit `int32` prints
  as `eax`, a 64-bit one as `rax`), or a `const`'s number. `{0}`, `{1}`, ... name operands by their position.
- `{name:k}` adds an x86 operand modifier: `b` (8-bit register), `w` (16), `k` (32), `q` (64), `x` (`xmm`), `t`
  (`ymm`), `g` (`zmm`), `c` (a bare constant).
- `{{` and `}}` are literal braces; a `$` needs no escaping.
- A template that names an operand the block does not declare is an error (`the asm template names '{n}', which is
  not an operand: declare it with in(...), out(...), inout(...) or const`). An operand the template never names is
  allowed (a register an instruction uses implicitly, such as `out("eax")` after `rdtsc`).

### 1.3 Options

| Option | Meaning |
|---|---|
| `att_syntax` | the template is AT&T syntax (the default is Intel: `mov dst, src`, no `%` or `$` prefixes) |
| `nomem` | the block reads and writes no memory (no memory clobber; the optimizer may keep values in registers across it) |
| `readonly` | the block reads memory but writes none |
| `pure` | the block has no effect but its outputs: it may be removed when they are unused, or merged with an identical block. Needs `nomem` or `readonly`, and at least one output |
| `nostack` | the block pushes nothing on the stack (no stack realignment around it) |
| `preserves_flags` | the block leaves the flags as it found them |
| `noreturn` | control never comes back (code after the block is unreachable); the block may have no outputs |

Without options a block may read and write any memory, change the flags and use the stack, and is never removed or
moved past other memory accesses.

## 2. File-scope `asm`

**Status:** **Implemented**

```c4
asm {
	".globl c4_answer";
	"c4_answer:";
	"mov eax, 42";
	"ret";
}
[link("c4_answer")] int32 Answer();
```

At file scope an `asm` block adds its lines to the program's assembly as they are: labels, data, whole functions
(declare them with `[link("symbol")]` to call them). It takes only template lines and the `att_syntax` option;
operands are an error there.

## 3. Rules

- `asm` is a keyword.
- The target is x86-64 (Windows x64 today); an `asm` block is written for that target.
- Assembly the assembler rejects is a compile error with the assembler's message and the offending line
  (`<inline asm>:2:2: error: invalid operand for instruction` / `mov rax, ecx`): the operand sizes it picked are the
  operands' types, so a 32-bit value in a 64-bit instruction is an error, not a silent truncation.
- Operand names are unique within a block; `in`, `out`, `inout`, `clobber` and `clobber_abi` are words only inside it.
- An output's location is any assignable location of the operand's type (a variable, field, element or `*p`);
  writing through it obeys the usual rules (no output into a const location).
