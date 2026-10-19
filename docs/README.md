# IED planning docs

Planning for the implementing AI (and the owner). `IDEplan.md` at the repo root is the source of truth for intent; if a doc disagrees with the plan, the plan wins and the doc gets fixed. Frozen formats live in `shared/host-api/` (schemas); docs explain them.

## Reading order

1. `IDEplan.md` - the vision.
2. [08-user-stories](08-user-stories.md) - personas, core journeys J1-J14, stories by epic.
3. [09-feature-catalog](09-feature-catalog.md) - the full feature set (index + "surpass Echo" differentiators), then the area files in [features/](features/).
4. [18-roadmap](18-roadmap.md) - milestones M0-M11 and what can start before the compiler is ready.
5. The design docs below for whatever you are about to build.
6. [99-open-questions](99-open-questions.md) - decision record and what is still open.

## Design docs

| Doc | Contents |
|---|---|
| [01-architecture-overview](01-architecture-overview.md) | Server/client split, multi-workspace, server modules, plugin runtimes, storage |
| [02-plugin-architecture](02-plugin-architecture.md) | Four plugin runtimes, locations and trust, lifecycle, contribution points, bindings, permissions |
| [03-project-and-solution-model](03-project-and-solution-model.md) | Solution/project/federation files, folder and file modes, the `.ied/` folder |
| [04-ai-chat-design](04-ai-chat-design.md) | Providers, model groups and roles, scheduling/routing, privacy, Claude, context, skills, MCP, egress, persistence |
| [05-default-plugins](05-default-plugins.md) | The six built-in plugins and how they compose |
| [06-build-and-nuke-integration](06-build-and-nuke-integration.md) | Task model, `${var}` injection, output sources, problem matchers, tests, LSP/build sync, Nuke work items |
| [07-settings-and-theming](07-settings-and-theming.md) | Settings tiers and window, theme tokens (proposed values), syntax palettes, typography |
| [10-server-protocol](10-server-protocol.md) | WebSocket protocol v1: frames, topics, resume, backpressure, document editing, CLI pipe |
| [11-security-and-multi-client](11-security-and-multi-client.md) | Threat model, access defaults, trust layers, AI permissions, secrets, concurrency rules |
| [12-agent-runtime-and-reliability](12-agent-runtime-and-reliability.md) | Run state machine, loop, modes, **stall/stop protection**, tools execution, sub-agents, checkpoints, budgets |
| [13-ai-tool-catalog](13-ai-tool-catalog.md) | Every AI tool with shape, class, group; mode presets; result budgets |
| [14-system-prompt](14-system-prompt.md) | System message assembly and the shipped prompt texts (`src/ai/prompts/`) |
| [15-ui-shell-and-interaction](15-ui-shell-and-interaction.md) | Desktop/phone wireframes, docking, VS keymap, context menus, interaction rules |
| [16-profiler-and-trace-format](16-profiler-and-trace-format.md) | Profiler architecture and the ITP v1 trace protocol |
| [17-c4-readiness-and-cross-repo-work](17-c4-readiness-and-cross-repo-work.md) | What C4/Bedrock must provide, suspected C4 bugs, C4LSP/C4DAP/Nuke/C4Assist/Bedrock/Claymore work items |
| [19-testing-strategy](19-testing-strategy.md) | Test layers, mock and fake-model servers, resume tests, screenshots, budgets, gates |
| [20-echo-parity-and-migration](20-echo-parity-and-migration.md) | Echo parity floor, how IED surpasses each item, migration importer |
| [21-distribution-and-bundled-services](21-distribution-and-bundled-services.md) | Install shape, on-disk layout, bundled vs detected components, SearXNG/tor packaging, first run |

## Doc conventions

- Numbered `NN-name.md`, one concern per doc; feature catalog areas in `features/NN-area.md`.
- Stable IDs: stories `US-<EPIC>-n`, journeys `J<n>`, features `F-<AREA>-n`, tools `T-<GROUP>-n`, C4-repo work items (`N1`, `L1`, `S1`, `P-1`). Never renumber; retire with a strike-through.
- ASCII only in new and touched docs (family convention).
- Decisions go into the owning doc and get a one-line record in `99-open-questions.md`.
- Update `WORK.md` when a doc materially changes what is next.
