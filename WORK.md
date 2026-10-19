# WORK.md - current state and next actions

Living document. Update it at every checkpoint commit.

## Current state (planning complete for v1 scope; no product code)

- [x] `IDEplan.md` - vision (owner-maintained).
- [x] Folder structure, root conventions (`README.md`, `AGENTS.md`, `.gitignore`), third-party deps pre-downloaded (`tools/fetch-deps.ps1`).
- [x] **User stories and journeys** - `docs/08` (personas, journeys J1-J14, 178 stories in 24 epics, tagged plan/plan+/ext and P0/P1/P2).
- [x] **Full feature catalog** - `docs/09` + `docs/features/` (16 areas, 546 features - 247 P0, 240 P1, 59 P2 - with owners and story links; "surpass Echo" differentiators; 89 AI tools in `docs/13`).
- [x] **Blocking decisions resolved as drafts** - protocol v1 (`docs/10`), security and multi-client (`docs/11`), manifest/solution/project/federation/agent-mode/frames schemas (`shared/host-api/`, validated examples).
- [x] **Design docs** - agent runtime and stall protection (`docs/12`), tool catalog (`docs/13`), system prompt + shipped prompt texts (`docs/14`, `src/ai/prompts/`), UI shell with wireframes and keymap (`docs/15`), profiler + ITP trace protocol (`docs/16`), distribution (`docs/21`), Echo parity/surpass/migration (`docs/20`), testing (`docs/19`), roadmap M0-M11 (`docs/18`).
- [x] **C4 readiness gap analysis + cross-repo work items** - `docs/17` (Bedrock Net/Process/FS.Watch/Time/Text, suspected CBind bugs S1-S7, C4LSP L1-L9, Nuke N1-N9, C4Assist -> declarative `c4` plugin, Bedrock `Profile::`).
- [ ] No product code. The C4 compiler cannot build the server yet (`docs/17` readiness checklist).

## Next actions

**Owner:** review `docs/99` "Open - owner's call" (O1-O10) and the M0 deliverables below; sign off the schemas so they can be frozen.

**M0 - can start now (no compiler needed), in this order:**

1. Freeze `shared/host-api` v1 after owner review (manifest, solution, project, frames).
2. `frontend/css/theme.css` + static HTML mocks of the six review screens (`docs/15`); screenshots at 1920x1080, 1366x768, 390x844; owner sign-off.
3. `tools/mock-server/` (Python, protocol v1) + `tools/conformance/` suite.
4. Frontend shell on the mock server: sidebar, docking, menus/commands/keybindings, status bar, WS client with resume, phone shell - with Playwright e2e and resume tests.
5. `tools/fake-model/` + scripted cases for every stop class in `docs/12`.
6. SearXNG-on-Windows spike (`docs/21`).

**C4 repo (parallel, owner's other track):** verify suspected bugs S1-S7 and file/fix confirmed ones per `AGENTS.md`; Bedrock P-1 modules; scale test; Nuke N1-N6; the declarative `c4` plugin (schema-valid today: see `shared/host-api/examples/c4-plugin/`); C4LSP L1-L5.

**Then M1-M11** per `docs/18` once the readiness checklist in `docs/17` is green.

## Decisions log

| Date | Decision | Where recorded |
|---|---|---|
| 2026-10-19 | JS plugin runtime = **QuickJS-ng v0.17.0** (ChakraCore dropped: unmaintained, dead links); behind a C shim | `docs/99` #6, `docs/02` |
| 2026-10-19 | Third-party deps pre-vendored to `third_party/` (gitignored); SHA256 record in `tools/deps-manifest.json` | `tools/README.md` |
| 2026-10-19 | UI delivery = split server + served web frontend; WebView2 wrapper optional later | `docs/99` #1, `docs/01` |
| 2026-10-19 | Tracy is a feature reference only - never linked | `docs/05`, `docs/16` |
| 2026-10-19 | **Echo parity is the floor; IED must drastically surpass it** (owner) - orchestration area, differentiators raised to P1 | `docs/09`, `docs/20`, `docs/features/16` |
| 2026-10-19 | Plugin runtimes: builtin (compiled in) / declarative / JS / process - C4 has no DLL output | `docs/02`, `docs/17` |
| 2026-10-19 | Protocol v1: one WS per tab, topic seq + snapshots + gaps, opId idempotency, doc lease + rebase | `docs/10` |
| 2026-10-19 | Security: loopback default, Host/Origin checks, loopback one-time token, password required for LAN | `docs/11` |
| 2026-10-19 | Task variables use `${name}` (replaces `{name}`) | `docs/06`, schemas |
| 2026-10-19 | Git backend = git CLI; libgit2 kept vendored for later | `docs/99` #14 |
| 2026-10-19 | Claude: API-key provider + bridge driving the owner's own `claude` CLI; IED never handles Claude credentials (Anthropic policy) | `docs/04`, `docs/99` O2 |
| 2026-10-19 | Profiler trace protocol = ITP v1 (explicit interning, timestamped thread renames, task events) | `docs/16` |
| 2026-10-19 | SearXNG bundled via embeddable Python (spike pending); tor bundled; Edge via CDP for browser tools | `docs/21` |
| 2026-10-19 | Native APIs only through hand-written C shims with opaque handles until CBind struct bugs are verified/fixed | `docs/17` |
