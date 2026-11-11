# 32 - The planning board

## Purpose

Owner, 2026-11-05: "a new plugin that will be used for planning. this will be an infinite board that the user can draw on and add text to that an AI can view and refine the drawings and convert to some markup or mermaid diagram that can continue to be iterated on and eventually be executed on by an AI to build ... a tile based system and support multiple layers like in other drawing software. also an eraser and other drawing tools as well as an AI chat section", and "this should also include pasting in images and resizing/cropping them like in other drawing software".

The board is where software is designed by hand before it is built: sketch boxes and arrows with a pen, write on it, paste a screenshot, and let an AI turn the sketch into a clean diagram, a Mermaid flowchart and a written plan, iterate on all three with it, and hand the plan to an agent that builds it. The plugin is `ied.board` (`plugins/board`), a built-in: its server code (`plugins/board/code`) is compiled into the server, its UI module is `plugins/board/ui`.

The loop it serves:

```
sketch (pen, shapes, text, notes, images)
  -> AI looks (board_view: a picture with ids and a grid; board_read: the elements as text)
  -> AI cleans up (board_recognize turns strokes into shapes, frames and connectors; board_edit tidies)
  -> diagram <-> Mermaid (board_export / board_import, both ways, keys, positions, descs, notes and colors kept)
  -> plan (board_plan: Markdown with the live diagram in it)
  -> Build (an agent chat given the plan)
```

## Files

A board is a file, `*.iedboard`, anywhere in the workspace (committed like any design document; `design/checkout.iedboard`). It is JSON written one element per line, so a diff shows which elements changed:

```
{
"format": "ied.board/1",
"title": "Checkout service",
"nextId": 58,
"layers": [
{"id":"L1","name":"Sketch","visible":false,"locked":false,"opacity":1},
{"id":"L2","name":"Diagram","visible":true,"locked":false,"opacity":1}
],
"elements": [
{"id":"e1","layer":"L1","type":"stroke","color":"red","width":3,"pts":[102.5,80,104,81.5]},
{"id":"e33","layer":"L2","type":"rect","x":100,"y":80,"w":160,"h":60,"text":"API Gateway","key":"api","color":"blue"},
{"id":"e40","layer":"L2","type":"connector","from":"e33","to":"e34","text":"writes"}
],
"plans": [
{"id":"p1","title":"Plan","kind":"markdown","text":"# Checkout service\n..."}
]
}
```

Images live beside their board: `design/checkout.assets/<16 hex digits of the image's SHA-256>.png`. Per-machine state - the chat a board talks to, the camera - lives in `.ied/local/boards/<hash>.json`, never in the file, and the board's history beside it in `<hash>.history.json` ("Changes" below); the pictures the AI's tools render go to `.ied/local/boards/views/` (the newest 60 kept).

## Coordinates and tiles

World units are CSS pixels at 100% zoom, y down, numbers kept to one decimal. The board is unbounded and tiled:

- **The server** indexes each board's elements by 512-unit tiles (tile -> the elements whose bounds touch it, `Board::Near`). Region reads, pictures, hit tests and the recognizer's neighbours visit only the tiles they cover.
- **The client** draws the board as tiles (`ui/canvas.js`): a tile is rendered once into an offscreen bitmap for its scale (512 CSS pixels a side, so 512 / scale world units) and reused while panning; it holds the background, the dot grid and every visible layer (a layer below full opacity is drawn apart and laid on at its opacity), so it is opaque and its text gets the screen's subpixel antialiasing. Once zooming settles (150 ms after the last step) tiles are rendered at the camera's own zoom and drawn on whole device pixels, unscaled, into a canvas sized to its box in device pixels - nothing on the board is resampled; while a zoom goes on, the nearest power-of-two tiles are drawn scaled, so a wheel or pinch never draws the board again at every step. An edit invalidates only the tiles its element's old and new bounds touch, at every scale; tiles of a zoom the camera has left are dropped, and the least recently used past 320. What is being drawn, moved, erased or typed is drawn live above the tiles.

## Layers

Ordered layers, bottom to top, each `{id, name, visible, locked, opacity}`: add, rename, reorder, show or hide, lock, opacity, delete (with its elements), merge down. Every element is on one layer; drawing goes to the active layer; a locked layer takes no edit from the owner's pointer (the AI is told it is locked and may still change it when asked). Order within a layer is the elements' order.

The AI's convention (`skills/board`): the owner's freehand stays on its layer; a cleaned-up diagram goes on a layer of its own ("Diagram") and the sketch layer is hidden, every stroke kept.

## Elements

| type | fields | notes |
|---|---|---|
| `stroke` | `pts` (x, y pairs), `width`, `color`, `opacity` | freehand ink; the highlighter is a wide stroke at opacity 0.35 |
| `rect`, `round`, `ellipse`, `diamond`, `cylinder`, `hexagon`, `parallelogram` | `x, y, w, h`, `color`, `fill`, `width`, `style`, `text`, `fontSize` | **nodes**: the shapes a diagram is made of; `text` is the label, centered and wrapped |
| `text` | `x, y, w, h`, `text`, `fontSize`, `color` | free text; `w, h` its measured box |
| `note` | `x, y, w, h`, `text`, `fill` | a sticky note |
| `frame` | `x, y, w, h`, `text` (its title) | a group: it holds the elements of its own layer whose centers are inside it (a Mermaid `subgraph`) |
| `image` | `x, y, w, h`, `src`, `iw`, `ih`, `crop` | a picture file of the workspace, its size in pixels, the part shown (`[x, y, w, h]` in its pixels) |
| `line`, `arrow` | `pts`, `start`, `end` (`none`, `arrow`, `dot`) | free lines and arrows, bound to nothing |
| `connector` | `from`, `to` (element ids), `text` (label), `start`, `end`, `route` (`straight`, `elbow`) | an edge; its ends follow the elements it binds |

Every element has `id` (`e<n>`, short so the AI can say it), `layer`, `type`, and optionally `color` and `fill` (palette names: `ink`, `gray`, `red`, `orange`, `amber`, `yellow`, `lime`, `green`, `teal`, `cyan`, `blue`, `indigo`, `violet`, `pink`; `fill` also `none`), `style` (`solid`, `dashed`, `dotted`), `key` (its Mermaid id, kept across export and import), `desc` (what it is for: in its tooltip, the plan and the Mermaid), `about` (a note's or text's subject: it moves with it and exports by it) and `by` (`owner` or `ai`). Colors are names, never values: the client draws them with the theme's `--hue-*` tokens (`docs/07`) and the server's renderer reads the same tokens from `frontend/css/theme.css`.

## Changes, undo and the server's ownership

The server owns every open board (`AGENTS.md` rule 2): the browser and the AI change it through the same operations (`code/core/Ops.cf`, `LayerOps.cf`), each change is applied under the board's lock inside `PluginContext::Atomically` and pushed to every client on the board's topic, the file is written 0.8 s after the last change (and when the plugin stops), and a file changed on disk by something else is read again and pushed whole (`reloaded`).

A **change** is a list of operations applied at once - all of them or, when one is refused, none:

| op | fields |
|---|---|
| `add` | the element's fields at the op's top level; `ref` names it for later ops (`"@api"`); a node without `w, h` is sized to its text, without `x, y` placed beside the board's content; a frame's `around` fits it around ids and moves anything else inside out |
| `update` | `id` (or `ids`) and the fields to set (`null` clears one; `w`/`h` null refit to the text; `type` too); a frame takes `around` or `fit: true` |
| `insert` | `on` (a connector) and a node's fields: the node goes into the connector's path, the connector becomes two, what lies past it moves on to make room |
| `delete` | `ids` (connectors bound to them go too) |
| `move` | `ids` (or `layer`), `dx`, `dy` (a frame moves its members) |
| `order` | `ids`, `to`: `front`, `back`, `forward`, `backward` |
| `place` | `ids`, `near`, `side` (`right`, `left`, `above`, `below`), `gap`, `align`; or `between: [a, b]` |
| `layout` | `ids` (default: the nodes of `layer`, else of the visible layers), `direction` (`TD`, `LR`, `BT`, `RL`), `gap` (widened for connector labels), `keep` (nodes that stay; the rest is placed around them); frames keep their members together and others out; notes move with their node |
| `tidy` | the arrangement kept: nodes nearly in a row or column lined up on its center, snapped to `grid`; `same_size` gives nodes their middle size |
| `align` | `ids`, `edge` (`left`, `right`, `top`, `bottom`, `centerx`, `centery`) to the selection's bounds or `to` an element |
| `distribute` | `ids`, `axis` (`x`, `y`): equal gaps |
| `add_layer`, `update_layer`, `delete_layer`, `merge_layer`, `title` | as named |

Only `layout` and `tidy` refit frames around their members; a member moved, placed or aligned out of its frame leaves it. A change's answer lists each op's result, where every box it touched ended up, the ids its refs got, its id, and warnings: frames and what they hold now, elements placed over others, connectors that run through a node.

Every change records each element's state before and after. `undo` takes back the latest change of its author that is not undone (the owner's Ctrl+Z never undoes the AI's work), `redo` repeats it, and `board_revert` / the History panel's Revert take back any one change. Undo merges by field: it sets back only the fields the change set, so what others changed since in other fields stays; redo restores what the element was when it was undone, merged the same way. History keeps the last 500 changes. It survives a restart: each time the board's file is written, the history is written after it (`code/core/History.cf`: every change's element states, layers and title, which are undone, the next change's number) with the SHA-256 of the board text it goes with, and a board opened again takes it back when its file is still that text - a file changed while the server was not running (an edit, a checkout) starts its history empty, since the recorded states would no longer be what is on the board. A board read again while the server runs because its file changed on disk starts an empty history too.

The client applies its own `add`, `update`, `delete` and `move` at once and keeps them pending until the server's event for them arrives (`opId`); new elements get temporary ids (`t<n>`) the event maps to the board's, and a later edit naming a temporary id waits for that mapping.

## Protocol

Methods (`docs/10`; a plugin's own names are `ied.board/<method>`):

| Method | Params | Result |
|---|---|---|
| `ied.board/list` | `wsId` | `{groups: [{name, boards: [path]}], boards: [{path, boardId, title?, elements?, layers?, plans?}]}` (counts for the open ones; the groups below) |
| `ied.board/groups` | `wsId`, `groups: [{name, boards: [path]}]` | the whole arrangement written to `.ied/boards.json`, then the list as `ied.board/list` answers it; `bad_request` for a group with no name, a name over 80 characters or two of one name |
| `ied.board/create` | `wsId, path, title?` | `{boardId, path, topic}`; `conflict` when the file exists |
| `ied.board/open` | `wsId, path` | `{boardId, path, topic}` |
| `ied.board/edit` | `boardId, ops, opId?, label?` | `{rev, change, refs, done, warnings}`; `bad_request` naming the refused op |
| `ied.board/undo`, `ied.board/redo` | `boardId` | `{rev, change}` or `{nothing: true}` |
| `ied.board/revert` | `boardId, change, redo?` | `{rev, change}` |
| `ied.board/plan` | `boardId, action: set \| delete, id?, title?, kind?, text?` | `{id}` |
| `ied.board/image` | `boardId, blobId` (an upload, `docs/10` `PUT /w/<wsId>/api/upload`) | `{src, iw, ih}`: the PNG or JPEG kept beside the board |
| `ied.board/tool` | `wsId, tool` (export, import, recognize, read, view), `args` | the tool's `{text, image?}`, as the owner |
| `ied.board/local` | `boardId, set?` | the board's per-machine state (`{chatId, camera}`) |

Topics: `ied.board/ws:<wsId>` - `{boards}`, event `boards.changed`; `ied.board/b:<boardId>` - snapshot `{boardId, path, rev, title, layers, elements, plans, history}`, events `changed {rev, change: {id, by, label, timeMs, rev, elements, undone}, put: [{at, el}], removed: [id], layers?, title?, refs?, opId?}`, `plans {plans}`, `reloaded` (a snapshot).

## The board in the UI

The Boards activity has the Boards tool window (the workspace's boards, live, in the owner's groups - "Groups" below; New board; on a phone the Boards pane under More, so the bar keeps the Solution Explorer), the **Board** tool window under it (the side panel below, for the board worked on now) and takes `board:<path>` documents: a `.iedboard` opens as a board wherever a file opens - the Boards list, the Solution Explorer, Go To All, `ied open` - through the `fileViewers` point (`docs/02`), and Open With... shows its JSON in the text editor instead; a new empty `.iedboard` (New File) opens as an empty board named after its file. A board document:

- **The toolbar**: Select (V), Hand (H), Pen (P), Highlighter (I), Eraser (E; Partial cuts through strokes, Whole takes strokes and shapes; its size), Rectangle (R), Ellipse (O), Diamond (D), Line (L), Arrow (A), Connector (C), Text (T), Note (N), Frame (F); the palette (Shift+click: the fill alone) and Fill; line widths; Insert image; Undo, Redo; Snap to alignment (the magnet: `board.snap`, "Snapping" below); zoom out, zoom (click: 100%), zoom in, show everything; AI (the board's chat, below); Layers, plan and history (the Board tool window; on a phone its sheet). A board narrower than its toolbar - its chat beside it - wraps the toolbar onto a second row, the view's end (zoom, AI, the panel) kept together on the right; a phone's toolbar scrolls sideways instead, AI and the panel first.
- **The canvas**: the wheel and pinch zoom about the pointer; Shift+wheel or a sideways wheel pans sideways, space-drag, the middle or right button or the Hand pan; a dot grid. Click or box to select (Shift adds), drag to move (a frame takes its members), handles resize (an image keeps its aspect unless Shift), Delete, Ctrl+D, Ctrl+C/X/V (board elements, images, text), arrows nudge (Shift: 10), [ and ] order, Ctrl+A, Shift+1 fit, Shift+2 the selection; double click types into a node, note, frame, text or connector (Escape or Ctrl+Enter keeps it), double click an image to crop it (drag its edges; Enter keeps, Escape cancels), double click the empty board for text. A new shape opens its label at once. Images are pasted, dropped or inserted: converted to PNG in the browser (at most 4096 pixels a side), uploaded and kept beside the board.
- **The side panel**: **Layers**, **Plan** (the plans; edit in place, saved as you type; From board writes the Mermaid export as a plan, To board draws a plan's Mermaid), **History** (each change with its author, Revert or Redo). Owner, 2026-11-11: "the side panel with the layers and the history etc could be moved to a panel under the list of boards. this should help to give more space to the board its self." It is the Board tool window (`ied.board.panel`), docked under the Boards list (`dockGroup` board): it shows the board worked on now - the one whose tab was shown or touched last, else another open one, else "Open a board..." - and the tab chosen (`ied.board.panel` in the browser's storage) is the same for every board. A layout saved before the window existed gets it once per device, when a board first shows in the Boards activity; the toolbar's button shows it again after it is closed. A phone has no room beside the board: the button opens the panel as a sheet over the lower half of the board instead.
- **The AI** (owner, 2026-11-11: "i should be able to open the chat window without it auto sending a message. we can get rid of the side bar that it create and have it just open the chat window"): the toolbar's AI button opens the board's chat beside the board (a tab of its own on a phone): a new chat the first time, "Board: <title>", the same one after (its id in the board's per-machine state). Nothing is sent: a chat with no turns yet gets "On the planning board <path> (selected: <ids>): " in its empty message box (`ied.chat.focusInput`, `docs/05`), for the owner to finish or clear; a box that already holds text keeps it. Cleaning up, Mermaid, the plan, a review or building it are asked for in the chat in the owner's words (the board skill tells the model how). The AI's changes arrive live, flash, and are marked in History. Before 2026-11-11 the side panel had an AI tab whose buttons sent canned messages, and the first opening sent "look at the board and wait"; both are gone.
- **The board's chat beside the boards** (owner, 2026-11-11: "by default the chat should show up in a smaller window ... the side chat window should be tied to the main set of windows so when i switch the board it will switch to the corresponding chat"): the chat opens in the document group beside the boards' - a new one to the right taking 30% of the width when there is none (`ied.tabs.openBeside` `size`, `docs/02`). It is **one chat tab for every board** (owner, 2026-11-11: "in the planning board it should be a single chat tab that will switch as you switch boards"): a board shown while it is open puts its own chat in it, in its place (`ied.tabs.replace`) - the one the board talked to before, else a new one - and any other board chat tab in the activity closes. The tab is tethered with every board open there (`docs/15` "Tethered tabs": clicking it never flips the board shown, and it closes with the last board), so a board opened from the list goes in among the boards, not into the chat's group. With the tab closed, switching boards opens none. What was typed in each board's chat stays with that chat (`docs/04` "Unsent messages"). A board's chat is kept in the Chats list's **Boards** section, closed until the owner opens it (`section`, `docs/04` "Chat sections"; a chat made before sections moves there when its board next shows), and starts with its details panel closed. At that width the composer's Model, Mode and Permissions wrap onto two rows.

### Snapping

Owner, 2026-11-10: "it would be nice if the planning tools had alignment snapping." What the pointer moves, resizes or draws snaps, as Visual Studio's designer snaplines do (`ui/snap.js`, pure functions of boxes; `ui/tools.js` gives the targets and draws the guides):

- **Targets**: the boxed elements (nodes, text, notes, frames, images) of the visible layers on the screen when the drag starts, but for what is being dragged and its connectors. Strokes, lines and connectors are not targets: a sketch's bounds line up nothing.
- **Lines**: a target's two edges and its center on each axis. Within 6 screen pixels (so at any zoom it feels the same), the dragged thing's nearest edge or center goes onto the nearest line; each axis snaps on its own.
- **Moving** a selection (a frame with its members) snaps its bounds' edges and center, and to **equal gaps**: centered between its nearest neighbours in its row (column), or at the gap a neighbour already keeps to the one beyond it, so a row of boxes stays evenly spaced.
- **Resizing** snaps the edges the handle moves (an image's aspect, or Shift's, is kept after); **drawing** a rectangle, ellipse, diamond, frame, line or arrow snaps its first corner (end) at the press and the other as it is dragged.
- **Guides**, in the info accent: a line through each edge or center the dragged thing shares, from end to end of the boxes on it; an equal gap as a span with end ticks and its size. They show only while dragging.
- **Off**: hold Alt while dragging to place freely (pressing or letting go of Alt takes effect at once, without moving); the toolbar's magnet turns snapping off and on for every board - the `board.snap` setting (User tier, default on; Settings > Planning Board). Arrow-key nudges and the AI's ops do not snap: the AI has `align`, `distribute` and `tidy`.

## Groups

Owner, 2026-11-11: "grouping boards in the planer". The Boards window shows the workspace's boards in **groups** the owner makes - folding sections, in the order the owner keeps, each with its boards in order - then the boards in no group under **Ungrouped** (with no group at all, one flat list as before). Groups are the workspace's, not a machine's: they live in the committed `.ied/boards.json` (`{"groups": [{"name", "boards": [path]}]}`), so a board stays a plain file and grouping never changes it; a board belongs to one group at most (the first that names it), a board that is gone keeps its place in the file (it comes back to its group from the trash) and is left out of the list. The list carries the groups (`ied.board/list`, `boards.changed` on `ied.board/ws:<wsId>`), and the file changed by hand or a pull changes the list too. `ied.board/groups` writes the whole arrangement (`plugins/board/code/Groups.cf`).

- **In the window** (`plugins/board/ui/boardlist.js`): a search box, New board and New group. A board is dragged onto a group's head (to its end) or onto a board of a group (before it), or onto Ungrouped (out of its group); its menu (right button, Shift+F10) has Open, Move to Group (each other group, New Group...) and Remove from Group. A group's head folds it (kept per device in `localStorage`); its menu has New Board in Group..., Rename Group..., Move Up, Move Down and Delete Group, which keeps its boards, ungrouped.
- **Checks**: `test_boards_are_grouped_in_the_workspaces_boards_file` (server checks), `boardgroups.test.js` (the sections and moves), e2e `board_groups` at desktop and phone width.

## AI tools

The tools are built around what a model is good and bad at: it reads structure well, it reads pictures well, it places things by coordinates poorly, and every round trip costs. Every element has a short id, every picture tags the ids it shows and carries a grid, the text listing groups the owner's strokes into named sketch groups with a guess each, recognition and placement are done by the tools rather than by the model's arithmetic, and one call makes many changes.

| Tool | Class | Does |
|---|---|---|
| `board_list` | read | the workspace's boards |
| `board_create` | write | a new board file |
| `board_read` | read | the board as text: layers; nodes (keys, labels, boxes, colors, descs); connectors; frames and what they hold; notes, text, images; free lines; the sketch's groups with guesses, fits, ends and what they hold; plans; checks (a decision with fewer than two ways out, a dangling connector, an unlabeled node). `region`, `layer`, `ids` narrow it |
| `board_view` | read | a picture (PNG) - the board, a region or around ids - with id and group tags (an arrow's at its middle) and a labeled grid; says which layers it drew |
| `board_recognize` | write | proposals for the sketch's groups, then (`apply`) one change: clean shapes, frames and connectors on a "Diagram" layer, labels read off the picture (`labels`), corrections (`types`, `ends`), `place: beside`, the sketch hidden, deleted or kept |
| `board_edit` | write | a change of any ops; `dry_run` tries it on a copy, `view` adds a picture after it |
| `board_export` | read | Mermaid (stable order, colors as `class` lines, descs and notes as comments) or a Markdown outline in flow order |
| `board_import` | write | Mermaid onto the board: merge keeps positions and places new nodes beside the node they connect to; `replace`; `dry_run` |
| `board_plan` | write | read, write (`from: mermaid \| markdown` writes an export), delete; `{{board:mermaid}}` and `{{board:outline}}` in a plan are the board as it is when read; a plan with Mermaid in it says whether it is in step |
| `board_revert` | write | takes back one change |

All name the board by its workspace path and work with no browser open. The binding `board.query {wsId, tool, args, by?} -> {text, image?}` answers every tool; the toolCalls go through it, as do other plugins and the evaluation harness. A system-message section tells the model the tools exist and to read the `board` skill, which holds the workflows.

## Rendering for the AI

`board_view` draws on the server, with no browser (`code/core/Raster.cf`, `Render.cf`): anti-aliased capsules for strokes and outlines (dashed or not), scanline-filled polygons for shapes and arrow heads, the Hershey simplex stroke font for text (`FontData.ch`; `plugins/board/NOTICE.txt`), PNG images scaled and cropped (JPEGs are drawn as a placeholder: the browser converts what it pastes to PNG), the theme's colors - into an RGBA image encoded with Bedrock's PNG writer. At most `max_size` pixels on its long side (1280), at most 4 pixels per board unit; the answer says the scale and suggests a smaller region when small text would not read.

## Recognition

`code/core/Recognize.cf`, shared by `board_read`'s groups and `board_recognize`:

1. Each stroke is measured: bounds, length, chord, closed (its ends meet relative to its size).
2. A closed stroke, or open strokes whose ends meet in a loop (a box in four lines), is classified by its corners against its bounds, its hull's area and its distance from the fitted ellipse: rectangle, diamond or ellipse.
3. A rectangle with other shapes inside is a **frame**.
4. An open stroke is an arrow when its own end hooks back (a one-stroke arrow) or short strokes make a V at an end (a separate head), else a line when straight. Each end goes to the shape group or node whose outline is nearest within reach - so an end inside a frame goes to the shape it touches, not the frame - and a line from a shape to itself loses its far end.
5. What is left is clustered: letters and words of one line join across a word's gap; small strokes are handwriting, the rest scribbles. Small "shapes" in a line of handwriting are letters.
6. Handwriting inside a shape is its label; in a frame's top band its title; beside an arrow's middle its label. A scribble still reports its longest stroke's ends, so a misread arrow can be corrected (`types {"g21": "arrow"}`).

## Mermaid

Export (`code/core/Diagram.cf`) writes `flowchart TD` (or `LR` when more connectors run across than down): frames as `subgraph key ["title"] ... end`, each node as `key["label"]` in its shape's syntax, connectors as `-->`, `---`, `-.->` (dashed), `==>` (wide), `<-->` with `|"label"|`, then `classDef`/`class` lines for colors (`class a,b blue`, `class a fill_blue`, `class a blue_fill` for both), `%% desc key: ...` and `%% note by key: ...` comments. Nodes come in the order they were made and edges by their ends, so a move does not reorder the text. A node without a key gets one from its label (`api_gateway`), the same every time.

Import (`MermaidParse.cf`, `MermaidImport.cf`) reads `flowchart`/`graph` with a direction, nodes in those shapes (quoted labels, `#quot;`, `<br/>`), every link form (`-->`, `---`, `-.->`, `==>`, `<-->`, `--o`, `-- text -->`, `-->|text|`, chains, `&`), `subgraph`s (nested), `class` lines with palette names and the board's own comments; `classDef`, `style`, `click` and `linkStyle` are left out with a note. Merge matches nodes by key, then by a label only one node has, keeps their places, and places a new node on the row of the node it connects to (or laid out with the chart, moved onto the kept nodes and pushed clear), on a 10-unit grid; connectors become the chart's (only the fields that differ are updated); frames refit around their subgraphs' nodes.

## Agent evaluation

`plugins/board/eval`:

- `boardctl.py` starts a private server on a scratch workspace (a signed-out Claude command, so nothing spends the owner's plan) and calls any board tool through `board.query` exactly as a chat's tool call does, printing what the model gets and the path of the picture.
- `sketch.py` draws scenarios as a hand would (wobbly lines that overshoot, boxes in one stroke or four, separate or hooked arrow heads, curved arrows, handwriting in the Hershey fonts with jittered, slanted letters) and writes the scenario's truth.
- `sweep.py` draws many seeds of each scenario and scores the recognizer against the truth.

Agents (Claude models given only the tool descriptions, the skill and that command line, not the source) did tasks the owner would ask for and reported every call, the result and candid feedback. Tasks: clean up a web-shop sketch into a diagram, Mermaid and a plan; clean up a game-engine sketch with a hand-drawn box around a group; change an existing diagram (insert a cache, rename, regroup, recolor) and round-trip it through Mermaid without losing the owner's layout; draw an architecture from a description with a frame, a note and a plan.

## Evaluation results

Three rounds, each followed by changes to the tools; the counts are tool calls for the same tasks.

| Task | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Engine sketch with a group -> diagram + Mermaid | 24 calls; 5 of 6 arrows wired to the group box, no frame, frame moves dragged hidden strokes | 7 calls (Sonnet), perfect; 17 (Opus) where a one-stroke arrow was missed | 13 calls with a 5.7k plan, nothing to correct |
| Web shop sketch -> diagram + Mermaid | 9 (Sonnet) to 19 calls (Opus), notes stranded by layout | - | - |
| Change a diagram + Mermaid round trip | 19 calls, layout pulled non-members into the new frame | 23 calls, same trap via `around`, crowded import placement | 18 calls (Sonnet), correct, frame traps the remaining cost |
| Architecture from a description + plan | - | 18 calls, a complete design and a 121-line plan | - |

Recognition over 30 hand-drawn sketches (`sweep.py`, seeds 20-34 of both scenarios): 225 of 225 boxes, 195 of 195 arrows wired, 15 of 15 frames, 30 of 30 sketches read perfectly.

What the runs changed, by what they hit:

- **Arrows inside a hand-drawn group box resolved to the box.** Ends now go to the nearest outline, the box becomes a frame, its top-band writing its title, a self-loop loses its far end.
- **A short arrow's separate head, a one-stroke hooked arrow** were misread: head sizes relative to short shafts, hooks found when the barbs are most of the stroke, scribbles report their ends, `types` and `ends` correct any group (and the proposal shows corrections before applying).
- **Frames**: membership is the frame's own layer only (moving one no longer dragged hidden strokes); `around` moves out what else lies inside; layout keeps frames together and others out; only layout and tidy refit frames; every change says what each frame holds.
- **No coordinates guessed**: `place` (beside, between), `insert` (into a connector, making room), `layout keep`, `tidy` (rows and columns by center, `same_size`), `align to`, final positions in every answer, `view: true` on an edit, `dry_run`.
- **Notes**: `about` ties a note to its node (layout moves it, exports place it), loose handwriting can become a note.
- **Mermaid**: stable order, colors, descs and notes survive the round trip, minimal connector updates, import dry run, new nodes beside their connected node.
- **Plans**: `from: mermaid | markdown`, `{{board:mermaid}}` live in a plan, a stale-diagram note.
- **Undo** merges by field: the owner's undo and redo no longer erase the AI's later edits (found by the server check).
- **Words**: proposals say NEEDS A LABEL and name the handwriting to read; warnings for labels, types or ends that matched nothing; grammar and counts.

## Checks

- C4 unit tests (`plugins/board/tests`, 24 cases): geometry, the op engine (refs, refusals, undo by author and by field, the file round trip, tiles), layout, recognition, Mermaid both ways, the evaluation-driven ops. `toolchains/c4/dev/C4.exe -test plugins/board/code/core plugins/board/tests`.
- Server checks (`tools/server-checks/tests/test_board.py`): changes on the topic with undo by author, the file written and read back after an outside change, an uploaded image kept and drawn, the tools through the binding, a chat turn (fake model) whose `board_edit` is the AI's and whose `board_view` picture reaches the model.
- Frontend unit tests (`frontend/tests/boardsnap.test.js`): snapping to edges and centers within the tolerance and not past it, the nearest line winning, equal gaps between neighbours and after a pair, the guides for given edges.
- The UI, driven in the browser pane at 1440x880 and 375x812: pen, rectangle with its label, connector, partial eraser splitting a stroke, pasted image and crop, the panels, the AI button opening the board's chat beside the board (a tab on a phone) with nothing sent and the board in its box, the same chat again with the owner's words kept; the Board window under the list in a fresh layout and placed once into a saved one, following the board shown (a layer added to one board, absent from the other), its hint with no board open, the phone's sheet; the chat in a new group at 30%, switching boards bringing the other board's chat (made the first time, in the Boards section) and back, closing a board closing its chat, a board chat made before sections moving into one, the Boards section closed under the owner's chats, a board chat's details panel closed at full width and its choice kept for that chat alone, the toolbar wrapped beside a narrow chat (1500x860 headless); snapping a moved box to two edges, to a row's equal gap, a resized edge and a drawn rectangle's corners, Alt mid-drag, the magnet off, a touch drag at phone width.

## Open items

- Other Mermaid diagram kinds (sequence, class, state) as plans; the board draws flowcharts.
- Live cursors of other devices; links from nodes to code (`file:line`).
