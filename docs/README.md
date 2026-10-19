# IED planning docs

High-level planning for the implementing AI. These are **directions and constraints, not final specs** — where a doc says "open question", resolve it during design and record the decision here (edit the doc, don't leave parallel notes). `IDEplan.md` at the repo root remains the source of truth for intent; if these docs and the plan disagree, the plan wins and this doc gets fixed.

| Doc | Contents |
|---|---|
| [01-architecture-overview](01-architecture-overview.md) | Host/frontend split, process model, IPC, why web frontend + C4 backend, module map |
| [02-plugin-architecture](02-plugin-architecture.md) | Manifest v1 sketch, contribution points, bindings, skills & tool calls, global vs project-local plugins |
| [03-project-and-solution-model](03-project-and-solution-model.md) | VS-style project files + solution file, root-folder indirection, the `.ied/` hidden folder layout |
| [04-ai-chat-design](04-ai-chat-design.md) | Chat runtime, model groups, context compression, AGENTS.md & system-message sections, MCP, skills search, web egress (SearXNG/tor) |
| [05-default-plugins](05-default-plugins.md) | Per-plugin feature breakdown: chat, editor, git, terminal, sandbox, profiler |
| [06-build-and-nuke-integration](06-build-and-nuke-integration.md) | How IED drives Nuke: command templates, dropdown injection, output routing, LSP options sync |
| [07-settings-and-theming](07-settings-and-theming.md) | Settings tiers (app/solution/project), theme tokens, color semantics |
| [99-open-questions](99-open-questions.md) | Blocking decisions that must be made before implementation starts |

## Doc conventions

- Numbered `NN-name.md`, one concern per doc.
- Each doc: **Purpose → Design → Open items**. Keep them skimmable; deep detail belongs in code comments and the eventual spec docs, not here.
- Update `WORK.md` when a doc materially changes what's next.
