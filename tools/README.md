# tools/ - repo tooling

## Test doubles and checks (M0, `docs/18`, `docs/19`)

| Tool | Purpose | Run |
|---|---|---|
| [`mock-server/`](mock-server/README.md) | Protocol v1 (`docs/10`) with fixture data - workspace tree, documents with lease + rebase, scripted chat with tools and approvals, ANSI build runs, echo terminal - so the frontend can be built and resume-tested before the C4 server exists | `python -I tools/mock-server/mock_server.py`, console at `http://127.0.0.1:8765/_mock/` |
| [`conformance/`](conformance/README.md) | Black-box protocol suite (46 tests) for the mock server and the real one (which it signs in to with its launch token); needs the `testhooks.*` methods | `python -I tools/conformance/run_conformance.py [--url ws://... [--cookie name=value] or --server <exe>]` |
| [`security/`](security/README.md) | The real server's access rules (`docs/11`, 30 tests): Host and Origin checks, launch tokens, the session cookie, the password and its throttle, device revoke and logout, persistence, the LAN bind rule, the audit log; each test starts its own server | `python -I tools/security/run_security.py --server <exe>` (`nuke conformance` runs both) |
| [`fake-model/`](fake-model/README.md) | Scriptable OpenAI-compatible model server: 40 scripts covering every `docs/12` stop class plus must-not-stop cases; `/props`, `/slots`, `/v1/models`, `/api/show` with configurable `n_ctx` and slots | `python -I tools/fake-model/fake_model.py`, `python -I tools/fake-model/probe.py --all` |
| `schema-check/` | Validates `shared/host-api` schemas (unknown keywords and dangling `$ref`s are errors) and the examples, or any `.ied.sln/.ied.proj/.ied.fed`, `ied-plugin.json`, `.ied/modes/*.json`, `*.frames.jsonl` | `python -I tools/schema-check/schema_check.py [path...]` |
| `pylib/` | Shared stdlib code: `jsonc.py`, `minischema.py` (the validator), `frames.py`, `ws.py` (RFC 6455 client + server framing) | - |
| `selftest.py` | Runs all of the above: pylib tests, schema-check, mock fixtures, conformance vs the mock, fake-model scripts (~40 s) | `python -I tools/selftest.py` |

Python is already part of the toolchain (`setup.bat` installs 3.12 into `vendor/python` for Nuke; these tools are also tested on 3.14). Everything here is **stdlib only**: `python -I` ignores user site-packages, so a pip dependency would silently be missing. Because `-I` also drops the script's folder from `sys.path`, every entry script adds `tools/pylib` (and its own folder) explicitly. Sources are ASCII with tab indentation.

`schema-check` notes: files are matched to schemas by name; JSONC comments (`//`, `/* */`) are accepted. The validator implements exactly the keywords the schemas use - `type, enum, const, pattern, required, properties, additionalProperties, items, minItems, maxItems, uniqueItems, minimum, maximum, oneOf, $ref` (local and cross-file) plus `anyOf, allOf, not, minLength, maxLength, exclusiveMinimum, exclusiveMaximum` - and treats any other keyword as a schema error, so a schema can never rely on something that is silently ignored. Patterns use Python `re` with `re.ASCII` (ECMA `\d`/`\w` are ASCII). `fixtures/docs10-examples.frames.jsonl` holds the `docs/10` frame examples.

## setup/ - everything that is not committed

`setup.bat` at the repository root runs `setup/setup.py` (after installing the private Python 3.12.10 into `vendor/python`). It is idempotent: re-run it any time.

```bat
setup.bat                         git submodules, LLVM for the C4 compiler, the web client's libraries
setup.bat --toolchain             ... and build the C4 compiler + the "dev" toolchain (nuke build -p C4)
setup.bat --with extras           ... and the optional packages (WebView2 SDK, SearXNG, Tracy profiler GUI)
setup.bat --add-to-path           put bin/ (the nuke launcher) on the user PATH without asking
setup.bat --list                  every package, its pin and whether it is installed
setup.bat --only llvm --force     re-download and re-extract one package
```

- **Pins and SHA256s live in `setup/deps.toml`** - the manifest of record. Bump a package by changing its `url` and `sha256` together, then `setup.bat --only <name> --force`. A download that does not match its hash is never installed.
- Downloads land in `vendor/` (gitignored; the archives in `vendor/_downloads/` are deleted after extraction unless `--keep-downloads`), except LLVM, which Frost expects at `C4/Frost/vendor/Compiler` (gitignored by `C4/.gitignore`).
- In a linked **git worktree**, packages marked `share-with-worktrees` (LLVM, 4 GB) become a directory junction to the main checkout's copy instead of a second download; `--force` replaces the junction, never the main copy.
- Sources that are git repositories are **submodules** instead (`.gitmodules`): `third_party/codicons`, `third_party/libgit2`, `third_party/quickjs-ng`, and the compiler's `C4/Frost/Projects/Profiling/tracy` and `C4/Frost/Projects/Core/Modules/yaml/yaml-cpp`.
- setup updates submodules with `core.longpaths=true` (yaml-cpp's bundled gtest and other fixtures pass Windows' 260-character limit when the checkout is in a deep folder), which is why the README clones without `--recursive` and lets setup do it.
- `third_party/libgit2` is not checked out by a clone or by setup (`update = none`): nothing uses it yet, and its test fixtures exceed Windows' 260-character path limit in deep folders. Fetch it when needed with `git -c core.longpaths=true submodule update --init --checkout third_party/libgit2`.

## Dependencies and why (as of 2026-10-20)

| Dependency | Version | How | Used by | Notes |
|---|---|---|---|---|
| LLVM (`clang+llvm-...-windows-msvc`) | 22.1.0 | download (core) | C4 compiler build (Frost), C4 toolchain's clang + lld-link | 860 MB archive, ~4 GB extracted |
| `monaco-editor` | 0.57.0 | download (ied) | editor plugin | npm tarball; `esm/` is the ESM build, `min/vs/` the minified one, `dev/vs/` for local dev |
| `@xterm/xterm` + addon-fit + addon-web-links | 6.0.0 / 0.11.0 / 0.12.0 | download (ied) | terminal plugin, output window | npm tarballs |
| `codicons` | v0.0.46-45 (`8853e0b`) | submodule | shared/ui-kit icon set, `frontend/mocks/build.py icons` | icons CC BY 4.0, code MIT |
| `quickjs-ng` | v0.17.0 | submodule | JS plugin runtime (`src/jsruntime/`) | chosen over ChakraCore: actively maintained, modern JS, small C sources. **ChakraCore was dropped**: unmaintained since 2020 and its official binary links are dead |
| `libgit2` | `42a89d7c` | submodule, **opt-in** (`update = none`) | git plumbing candidate (`src/git/`) | the git CLI is the backend (docs/99 #14); libgit2 stays as a possible later graph optimization |
| `Microsoft.Web.WebView2` | 1.0.4258.31 | download (extras) | optional desktop wrapper only | `build/native/include/WebView2.h` + libs. **Not a v1 dependency** - IED is a split server + web client (docs/01) |
| `searxng` | `d47cb96b` | download (extras) | AI web egress (docs/04) | a tarball, not a submodule: 4 template files have Windows-illegal names (`*.conf:socket`) that break checkout; setup skips them on extract. Packaging: docs/21 |
| Tracy profiler GUI | 0.12.0 | download (extras) | viewing traces of the compiler's C++ libraries | the client sources are the compiler's submodule; for IED, Tracy is a **feature reference only** (docs/16) |

## Gotchas learned while wiring this up (don't relearn them)

1. npm scoped packages download as `<name>-<ver>.tgz` from `registry.npmjs.org/@scope/name/-/`; `npm pack` produces the same bytes.
2. ChakraCore's aka.ms shortlinks are dead - do not resurrect that dependency.
3. SearXNG cannot be checked out on Windows (illegal path names); use the codeload tarball and skip the 4 bad entries.
4. Tracy lives at `wolfpld/tracy`, not `psymon/tracy`.
5. The embeddable Python pins `sys.path` in `python312._pth`; setup keeps the file but enables `import site` (Frost and Nuke rely on the `exit` builtin and normal startup).
