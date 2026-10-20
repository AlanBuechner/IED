# tools/ - repo tooling

## Test doubles and checks (M0, `docs/18`, `docs/19`)

| Tool | Purpose | Run |
|---|---|---|
| [`mock-server/`](mock-server/README.md) | Protocol v1 (`docs/10`) with fixture data - workspace tree, documents with lease + rebase, scripted chat with tools and approvals, ANSI build runs, echo terminal - so the frontend can be built and resume-tested before the C4 server exists | `python -I tools/mock-server/mock_server.py`, console at `http://127.0.0.1:8765/_mock/` |
| [`conformance/`](conformance/README.md) | Black-box protocol suite (46 tests) for the mock server now and the real server later; needs the `testhooks.*` methods | `python -I tools/conformance/run_conformance.py [--url ws://...]` |
| [`fake-model/`](fake-model/README.md) | Scriptable OpenAI-compatible model server: 40 scripts covering every `docs/12` stop class plus must-not-stop cases; `/props`, `/slots`, `/v1/models`, `/api/show` with configurable `n_ctx` and slots | `python -I tools/fake-model/fake_model.py`, `python -I tools/fake-model/probe.py --all` |
| `schema-check/` | Validates `shared/host-api` schemas (unknown keywords and dangling `$ref`s are errors) and the examples, or any `.ied.sln/.ied.proj/.ied.fed`, `ied-plugin.json`, `.ied/modes/*.json`, `*.frames.jsonl` | `python -I tools/schema-check/schema_check.py [path...]` |
| `pylib/` | Shared stdlib code: `jsonc.py`, `minischema.py` (the validator), `frames.py`, `ws.py` (RFC 6455 client + server framing) | - |
| `selftest.py` | Runs all of the above: pylib tests, schema-check, mock fixtures, conformance vs the mock, fake-model scripts (~40 s) | `python -I tools/selftest.py` |

Python is already part of the family toolchain (Nuke vendors 3.12; also tested on 3.14). Everything here is **stdlib only**: `python -I` ignores user site-packages, so a pip dependency would silently be missing. Because `-I` also drops the script's folder from `sys.path`, every entry script adds `tools/pylib` (and its own folder) explicitly. Sources are ASCII with tab indentation.

`schema-check` notes: files are matched to schemas by name; JSONC comments (`//`, `/* */`) are accepted. The validator implements exactly the keywords the schemas use - `type, enum, const, pattern, required, properties, additionalProperties, items, minItems, maxItems, uniqueItems, minimum, maximum, oneOf, $ref` (local and cross-file) plus `anyOf, allOf, not, minLength, maxLength, exclusiveMinimum, exclusiveMaximum` - and treats any other keyword as a schema error, so a schema can never rely on something that is silently ignored. Patterns use Python `re` with `re.ASCII` (ECMA `\d`/`\w` are ASCII). `fixtures/docs10-examples.frames.jsonl` holds the `docs/10` frame examples.

## fetch-deps.ps1 - third-party dependency pre-download

Downloads and extracts all third-party dependencies IED needs into `third_party/` (gitignored; reproduced on demand). Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-deps.ps1        # idempotent, skips cached
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-deps.ps1 -Force # re-download everything
```

- **Pins live in the `$PINS` block at the top of the script** - that's the manifest of record.
- `deps-manifest.json` (this folder, committed) records every archive + SHA256 after each run; `third_party/_archives/manifest.json` is the same data next to the archives.
- Layout: original archives in `third_party/_archives/`, extracted sources in `third_party/<dep>-<version>/`.

## What's vendored and why (as of 2026-10-19)

| Dependency | Version | Used by | Notes |
|---|---|---|---|
| `monaco-editor` | 0.57.0 | editor plugin | npm tarball; `esm/` is the ESM build, `min/vs/` the minified one, `dev/vs/` for local dev |
| `@xterm/xterm` + addon-fit + addon-web-links | 6.0.0 / 0.11.0 / 0.12.0 | terminal plugin, output window | npm tarballs (scoped names arrive as `xterm-*.tgz`) |
| `codicons` | v0.0.46-45 | shared/ui-kit icon set | VS-style file-type + UI icons; icons CC BY 4.0, code MIT |
| `quickjs-ng` (amalgam) | v0.17.0 | JS plugin runtime (`src/jsruntime/`) | single `quickjs-amalgam.c` + headers - the vendoring unit. Chosen over ChakraCore: actively maintained, modern JS, embeds as one C file. **ChakraCore was dropped**: unmaintained since 2020 and its official aka.ms binary links are dead (they now redirect to bing.com) |
| `Microsoft.Web.WebView2` | 1.0.4258.31 | optional desktop wrapper only | NuGet package: `build/native/include/WebView2.h` + libs for MSVC. **Not a v1 dependency** - IED is a split server + web client; this is for the later "installed app" window that embeds a webview pointed at the local server (docs/01). Runtime ships with Windows / Evergreen bootstrapper at deploy time |
| `searxng` | master @ d47cb96b | AI web egress (docs/04) | codeload tarball, NOT git clone: 4 template files have Windows-illegal names (`*.conf:socket`) that break checkout, sparse-checkout and `git archive`; bsdtar skips them on extract. Packaging decided: bundled with an embeddable Python (Windows spike pending) - see docs/21 |
| `tracy` | master (wolfpld/tracy) | **feature reference ONLY** | Reference for the profiler plugin's views (docs/16 defines IED's own trace protocol). **Never linked into IED or C4, not a runtime dependency.** Note the repo moved from psymon/tracy to wolfpld/tracy |
| `libgit2` | master @ 42a89d7c | git plumbing candidate (`src/git/`) | headers in `include/git2/`. Decided: the git CLI is the backend (docs/99 #14); libgit2 stays vendored as a possible later graph optimization |

## Gotchas learned while wiring this up (don't relearn them)

1. npm scoped packages pack as `<scope>-<name>-<ver>.tgz` (`@xterm/xterm` -> `xterm-xterm-6.0.0.tgz`).
2. ChakraCore's aka.ms shortlinks are dead - do not resurrect that dependency.
3. SearXNG cannot be git-cloned on Windows (illegal path names). Use the codeload tarball; expect bsdtar to exit 1 while skipping the 4 bad entries.
4. Tracy lives at `wolfpld/tracy`, not `psymon/tracy`.
