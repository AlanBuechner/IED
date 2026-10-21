# shared/ - code and contracts shared between server, client and plugins

## host-api/

**The contract** - see [`host-api/README.md`](host-api/README.md). v1 drafts exist for the plugin manifest, solution, project, federation and agent-mode files, the WebSocket frame schema, the process-plugin RPC (`ied-plugin-rpc/1`), the skill file format, plus validated examples (a game-shaped `MyGame` solution/project and the C4 language plugin as a declarative manifest). Freeze after owner review (M0, `docs/18`).

## ui-kit/

Shared web components used by the shell and every plugin UI module, built only on theme tokens (`frontend/css/theme.css`), served at `/ui-kit/` and imported by URL (`import { h } from "/ui-kit/dom.js"`). Built so far:

- `dom.js` - `h()` element building, `icon()` / `iconFrom()` (codicons served one SVG each at `/codicons/<name>.svg`, painted as masks; `iconFrom` also draws a file icon's spec), `replace()`, `isTextInput()`
- `fileglyphs.js` - the drawn glyphs of files, folders, projects and solutions, and which spec the plugins' `fileIcons` / `projectIcons` give a file or project (`docs/15` Icons)
- `ansi.js` - ANSI escape sequences in streamed text as styled runs
- `menu.js` - popup menus (context menus, menu bar, dropdowns; bottom sheets on phones), keyboard driven
- `tree.js` - a virtualized tree view with lazy children, VS keys and type-ahead
- `textedits.js` - the document protocol's edits applied to text (UTF-16 columns)
- `markdown.js` - GitHub-flavored Markdown to elements, text always as text nodes (the editor's preview, chat replies)
- `ui-kit.css` - their styles, plus buttons, chips, badges, inputs and list rows

Planned:

- tree view (Solution Explorer, git refs, file pickers) with virtualization and type-ahead
- tab strip (document well, chat tabs) with dirty/running/attention glyphs
- ANSI console renderer (Output window, task output, terminal-like views)
- context menu, dropdown, toolbar button, split/dock handles
- diff view primitives, virtualized list/table, toast and card (approval/question) components
- icon set: codicons for UI glyphs + a VS-style file-type icon set (`docs/15`)
