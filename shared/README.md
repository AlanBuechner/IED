# shared/ - code and contracts shared between server, client and plugins

## host-api/

**The contract** - see [`host-api/README.md`](host-api/README.md). v1 drafts exist for the plugin manifest, solution, project, federation and agent-mode files, the WebSocket frame schema, the process-plugin RPC (`ied-plugin-rpc/1`), the skill file format, plus validated examples (a game-shaped `MyGame` solution/project and the C4 language plugin as a declarative manifest). Freeze after owner review (M0, `docs/18`).

## ui-kit/

Shared web components used by the shell and every plugin UI module, built only on theme tokens (`frontend/css/theme.css`):

- tree view (Solution Explorer, git refs, file pickers) with virtualization and type-ahead
- tab strip (document well, chat tabs) with dirty/running/attention glyphs
- ANSI console renderer (Output window, task output, terminal-like views)
- context menu, dropdown, toolbar button, split/dock handles
- diff view primitives, virtualized list/table, toast and card (approval/question) components
- icon set: codicons for UI glyphs + a VS-style file-type icon set (`docs/15`)
