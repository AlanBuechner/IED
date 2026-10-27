# The C4 API: tools around C4

`C4Api.dll` gives tools around C4 - a linter, a documentation generator, a code browser, an engine's asset pipeline,
a third party's IDE support - what the compiler knows about a source, through a C interface. It is the compiler
itself (C4Lib, linked statically), so a tool reads C4 exactly as `C4.exe` does, and it is versioned with the
toolchain: a tool built against a workspace's toolchain reads the C4 of that toolchain.

| Piece | Where | Status |
|---|---|---|
| `C4Api.dll`, `C4Api.lib` (its import library), `C4Api.h` | `<toolchain>/C4Api/` (`C4/C4Api` in the IED repository) | **Implemented**: API version 1, a source's declarations and syntax diagnostics |
| A Nuke project depending on it: `depends = ["C4Api"]` | the c4 package's library (`plugins/c4/libraries/C4Api` in the IED repository) | **Implemented** |
| A sample tool: `c4-outline` | `plugins/c4/samples/c4-outline` in the IED repository | **Implemented**, tested by Nuke's integration tests |
| Name resolution, types, the program model of a whole project | | **Planned**: API versions after 1 |

---

## 1. Why a DLL with a C interface

**Implemented.** A static library of the compiler (C4Lib.lib with its C++ headers) would make every tool compile with
the exact clang, LLVM headers, LLVM libraries (gigabytes) and C++ libraries (Core, Reflection, ...) the compiler was
built with, and break on every compiler change, since C++ has no stable ABI. A DLL with a C interface carries the
compiler and the parts of LLVM it needs inside it and exposes plain functions and structs, so:

- any compiler and any language that calls C can use it: C, C++, C4 through `extern "c"` interop
  ([language/13-interop.md](../language/13-interop.md)), Rust, Go (cgo), Python (ctypes);
- the interface keeps its shape across compiler versions: `C4A_API_VERSION` in the header is the version a tool was
  built against, `c4a_api_version()` the one the DLL implements, and a DLL implements every version before its own;
- a tool ships `C4Api.dll` beside itself (about 4 MB) and needs nothing else of the toolchain.

## 2. The API (version 1)

**Implemented.** `C4Api.h`:

| Function | Does |
|---|---|
| `c4a_api_version()` | the API version the DLL implements |
| `c4a_compiler_version()` | the compiler it is (`"0.1.0"`) |
| `c4a_parse_source(&options)` | parses one C4 source - `options.text` with `options.length` bytes, or the file at `options.path` when `text` is null - with `options.defines` (`NAME` or `NAME=VALUE`, as after `C4.exe -D`) on top of a default build's predefined macros. The macro preprocessor and the parser alone: no include is read, no name resolved. Never null |
| `c4a_parse_read(parse)` | 0 when the file could not be read (the one diagnostic says why) |
| `c4a_diagnostic_count`, `c4a_diagnostic_at(parse, i, &out)` | the preprocessor's, then the lexer's and the parser's diagnostics: severity, 1-based line and byte column, message |
| `c4a_declaration_count`, `c4a_declaration_at(parse, i, &out)` | the declarations, depth first, each before what it holds: kind (namespace, class, struct, interface, enum, enum value, function, variable, union, alias), name as written (`Registry::Add` for a member defined out of its type), where it starts, ends and where its name is, and the index of the declaration it is in (-1 at file level). Function bodies declare nothing |
| `c4a_kind_name(kind)` | `"class"`, `"enum value"`, ... |
| `c4a_parse_destroy(parse)` | frees it and every string it handed out |

Strings are UTF-8 and owned by the DLL; one stays valid until the parse it came from is destroyed. The functions keep
no state between calls, so separate parses may run on separate threads.

## 3. Using it from a Nuke project

**Implemented.** The c4 language package of IED's plugins (`plugins/c4/nuke`) brings a library project named `C4Api`:
a prebuilt C/C++ library ([02-nuke.md](02-nuke.md), Nuke/README.md section 9c) made of the workspace's toolchain's
`C4Api/` folder. A C or C++ project that depends on it compiles with `C4Api.h` on its include path, links
`C4Api.lib`, and gets `C4Api.dll` copied beside its program:

```toml
# nuke.toml - a tool of your own
[workspace]
name = "my-tool"
toolchain = "0.2.0"          # the C4 the tool reads; any installed version that ships C4Api/, or "dev"

[project]
name = "my-tool"
kind = "executable"
language = "cpp"
depends = ["C4Api"]
```

```cpp
#include "C4Api.h"
#include <cstdio>

int main(int argc, char** argv)
{
	c4a_parse_options options = {};
	options.path = argv[1];
	c4a_parse* parse = c4a_parse_source(&options);
	for (uint32_t i = 0; i < c4a_declaration_count(parse); ++i)
	{
		c4a_declaration declaration;
		c4a_declaration_at(parse, i, &declaration);
		std::printf("%s %s (%u:%u)\n", c4a_kind_name(declaration.kind), declaration.name, declaration.name_at.line, declaration.name_at.column);
	}
	c4a_parse_destroy(parse);
	return 0;
}
```

`nuke build run -- file.cf` builds it and outlines `file.cf`. The sample `plugins/c4/samples/c4-outline` is this tool
grown up: the declarations as a tree, the diagnostics in the `file(line,col): error: message` form IED's error list
reads, `-D` definitions, an exit code, and a test project (`nuke test`) that checks it against its fixtures.

A toolchain from before the C API (0.1.0) has no `C4Api/` folder: the build stops with the files that are not there.

**From C4**, the header binds through interop ([language/13-interop.md](../language/13-interop.md)): the opaque
`c4a_parse*` becomes `void*`, the structs and enums are C4's. A C4 project names the toolchain's files itself, since a
C4 program takes in no C/C++ library's settings - the sample's `c4/` project is `c4-outline` written this way, checked
against the same fixtures:

```toml
[project]
name = "c4-outline-c4"
includes = ["${toolchain.dir}/C4Api"]          # where extern "c" include "C4Api.h" finds the header
links = ["${toolchain.dir}/C4Api/C4Api.lib"]

[[project.copy]]
from = "${toolchain.dir}/C4Api/C4Api.dll"
```

```c4
include "bedrock.ch"
extern "c" include "C4Api.h"

int32 main(int32 argc, char8** argv)
{
	c4a_parse_options options;
	options.path = argv[1];
	void* parse = c4a_parse_source(&options);
	int32 count = (int32)c4a_declaration_count(parse);
	c4a_parse_destroy(parse);
	return count;
}
```

**Without Nuke**, use the files directly: compile with `-I <toolchain>/C4Api`, link `<toolchain>/C4Api/C4Api.lib`
(MSVC ABI), and ship `C4Api.dll` beside the program.

## 4. In the IED repository

**Implemented.** `C4/C4Api` builds the DLL from source (`nuke build` builds it with the compiler, and
`MakeToolchain.py` ships it with its header); `C4/C4Lib/src/Outline` holds what it reports, tested in C4Testing
(`Outline.*`). In the repository's own workspace a project named `C4Api` is that source project, so the package's
prebuilt library only serves other workspaces. Adding to the API: a new function or struct field comes with a new
`C4A_API_VERSION`, never a change to what an older version promised.
