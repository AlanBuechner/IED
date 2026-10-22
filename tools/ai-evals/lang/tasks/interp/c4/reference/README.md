# calc

`calc <script>` runs a script written in calc, a small scripting language.

```
fn counter() {
    let n = 0;
    return fn () { n = n + 1; return n; };
}
let next = counter();
next();
print("count:", next());   // count: 2
```

## Values

| Type | Written | Notes |
|---|---|---|
| `int` | `42` | 64-bit integers |
| `string` | `"text"` | escapes `\n`, `\"` and `\\`; a string ends on the line it starts |
| `bool` | `true`, `false` | |
| `nil` | `nil` | what a function returns when it returns nothing |
| `array` | `[1, "two", [3]]`, `[]` | arrays are shared, not copied: `let b = a;` makes `b` the same array as `a` |
| `function` | `fn (a, b) { ... }` | functions are values: they can be stored, passed and returned |

`false` and `nil` count as false in a condition; everything else, `0` and `""` included, counts as true.

## Statements

| Statement | Meaning |
|---|---|
| `let x = e;` | declares `x` in the current scope (declaring it again in the same scope replaces it) |
| `x = e;` | assigns to the existing `x` of the nearest scope that has one |
| `if (c) { ... } else if (c) { ... } else { ... }` | the braces are required |
| `while (c) { ... }` | runs the body, a block, as long as `c` is truthy |
| `a[i] = e;` | sets the element `i` (from 0) of the array `a` |
| `fn name(a, b) { ... }` | declares a function in the current scope |
| `return e;`, `return;` | leaves the function (`return;` returns `nil`); only inside a function |
| `{ ... }` | a block: a scope of its own |
| `e;` | evaluates an expression, such as a call |

`//` starts a comment that runs to the end of the line.

## Expressions

From the loosest binding to the tightest:

| Operators | |
|---|---|
| `\|\|` | true when either side is truthy; the right side is evaluated only when the left one is not |
| `&&` | true when both sides are truthy; the right side is evaluated only when the left one is |
| `==` `!=` | any two values: values of different types are never equal, functions only to themselves |
| `<` `<=` `>` `>=` | integers |
| `+` `-` | integers; `+` also joins two strings |
| `*` `/` `%` | integers; `/` and `%` truncate toward zero |
| `-x` `!x` | negation of an integer; `!` of any value |
| `f(a, b)`, `a[i]` | a call; an element of an array |

Binary operators of one level group to the left: `10 - 4 - 3` is `3`.

A function sees the variables of the scope it was defined in (it is a closure), also when it is called after
that scope has ended.

## Builtins

- `print(a, b, ...)` writes its arguments separated by one space, then a line break. Strings are written without
  quotes, `nil` as `nil`, functions as `<fn name>` (`<fn>` when anonymous) and builtins as `<builtin name>`.
  Arrays are written as `[1, 2, 3]`, their elements as `print` writes them.
- `str(x)` is `x` as `print` would write it.
- `len(x)` is the number of elements of an array or of bytes of a string.
- `push(a, x)` appends `x` to the array `a` and returns `nil`.

## Errors

An error stops the script and prints `error: line N: <message>` to standard error, with exit code 1: characters
that are not part of the language, a statement that does not parse (`expected ';' but found 'print'`), and at run
time an undefined variable, an operator applied to the wrong types (`cannot apply '+' to string and int`), a call
of something that is not a function (`cannot call int`), a call with the wrong number of arguments
(`expected 2 arguments but got 1`), division by zero, an index outside an array (`index out of range`) or that
is not an int (`index must be an int`), indexing something that is not an array (`cannot index int`), and `len` or
`push` given the wrong type (`len expects an array or a string`, `push expects an array`).
A missing or extra command-line argument prints `usage: calc <script>` with exit code 2.
