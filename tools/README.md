# tools/ — repo tooling

## fetch-deps.ps1 — third-party dependency pre-download

Downloads and extracts all third-party dependencies IED needs into `third_party/` (gitignored; reproduced on demand). Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-deps.ps1        # idempotent, skips cached
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fetch-deps.ps1 -Force # re-download everything
```

- **Pins live in the `$PINS` block at the top of the script** — that's the manifest of record.
- `deps-manifest.json` (this folder, committed) records every archive + SHA256 after each run; `third_party/_archives/manifest.json` is the same data next to the archives.
- Layout: original archives in `third_party/_archives/`, extracted sources in `third_party/<dep>-<version>/`.

## What's vendored and why (as of 2026-10-19)

| Dependency | Version | Used by | Notes |
|---|---|---|---|
| `monaco-editor` | 0.57.0 | editor plugin | npm tarball; `esm/` is the ESM build, `min/vs/` the minified one, `dev/vs/` for local dev |
| `@xterm/xterm` + addon-fit + addon-web-links | 6.0.0 / 0.11.0 / 0.12.0 | terminal plugin, output window | npm tarballs (scoped names arrive as `xterm-*.tgz`) |
| `codicons` | v0.0.46-45 | shared/ui-kit icon set | VS-style file-type + UI icons; MIT |
| `quickjs-ng` (amalgam) | v0.17.0 | JS plugin runtime (`src/jsruntime/`) | single `quickjs-amalgam.c` + headers — the vendoring unit. Chosen over ChakraCore: actively maintained, modern JS, embeds as one C file. **ChakraCore was dropped**: unmaintained since 2020 and its official aka.ms binary links are dead (they now redirect to bing.com) |
| `Microsoft.Web.WebView2` | 1.0.4258.31 | optional desktop wrapper only | NuGet package: `build/native/include/WebView2.h` + libs for MSVC. **Not a v1 dependency** — IED is a split server + web client; this is for the later "installed app" window that embeds a webview pointed at the local server (docs/01). Runtime ships with Windows / Evergreen bootstrapper at deploy time |
| `searxng` | master @ d47cb96b | AI web egress (docs/04) | codeload tarball, NOT git clone: 4 template files have Windows-illegal names (`*.conf:socket`) that break checkout, sparse-checkout and `git archive`; bsdtar skips them on extract. Packaging (bundled binary vs docker) is design-phase work — see docs/99 #8 |
| `tracy` | master (wolfpld/tracy) | **feature reference ONLY** | Reference for the profiler plugin's views + trace format (docs/05). **Never linked into IED or C4, not a runtime dependency.** Note the repo moved from psymon/tracy to wolfpld/tracy |
| `libgit2` | master @ 42a89d7c | git plumbing candidate (`src/git/`) | headers in `include/git2/`. Decision pending: libgit2 vs shelling out to the git CLI (docs/05) — vendored either way so the comparison can happen offline |

## Gotchas learned while wiring this up (don't relearn them)

1. npm scoped packages pack as `<scope>-<name>-<ver>.tgz` (`@xterm/xterm` → `xterm-xterm-6.0.0.tgz`).
2. ChakraCore's aka.ms shortlinks are dead — do not resurrect that dependency.
3. SearXNG cannot be git-cloned on Windows (illegal path names). Use the codeload tarball; expect bsdtar to exit 1 while skipping the 4 bad entries.
4. Tracy lives at `wolfpld/tracy`, not `psymon/tracy`.
