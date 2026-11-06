# 32 - The planning board

## Purpose

Owner, 2026-11-05: "a new plugin that will be used for planning. this will be an infinite board that the user can draw on and add text to that an AI can view and refine the drawings and convert to some markup or mermaid diagram that can continue to be iterated on and eventually be executed on by an AI to build ... a tile based system and support multiple layers like in other drawing software. also an eraser and other drawing tools as well as an AI chat section".

The board is where software is designed by hand before it is built: sketch boxes and arrows with a pen, write on it, and let an AI turn the sketch into a clean diagram, a Mermaid flowchart and a written plan, iterate on all three with it, and hand the plan to an agent that builds it. The plugin is `ied.board` (`plugins/board`).

The loop it serves:

```
sketch (pen, shapes, text, notes)
  -> AI looks (board_view: a picture with ids; board_read: the elements as text)
  -> AI cleans up (board_recognize proposes shapes for strokes; board_edit applies)
  -> diagram <-> Mermaid (board_export / board_import, both ways, keys kept)
  -> plan (board_plan: Mermaid + a Markdown spec kept in the board)
  -> Build (a new agent chat given the plan)
```

## Files

A board is a file, `*.iedboard`, anywhere in the workspace (committed like any design document; `design/checkout.iedboard`). It is JSON written one element per line, so a diff shows which elements changed:

```
{
"format": "ied.board/1",
"title": "Checkout service",
"nextId": 58,
"layers": [
{"id": "L1", "name": "Sketch", "visible": true, "locked": false, "opacity": 1},
{"id": "L2", "name": "Diagram", "visible": true, "locked": false, "opacity": 1}
],
"elements": [
{"id": "e1", "layer": "L1", "type": "stroke", "color": "ink", "width": 2, "pts": [102.5, 80, 104, 81.5, ...]},
{"id": "e33", "layer": "L2", "type": "rect", "x": 100, "y": 80, "w": 160, "h": 60, "text": "API Gateway", "key": "api", "color": "blue"},
{"id": "e40", "layer": "L2", "type": "connector", "from": "e33", "to": "e34", "text": "writes"}
],
"plans": [
{"id": "p1", "title": "Plan", "kind": "markdown", "text": "# Checkout service\n..."}
]
}
```

Per-machine state - the chat a board talks to, the camera - lives in `.ied/local/boards/<hash>.json`, never in the file.

## Coordinates and tiles

World units are CSS pixels at 100% zoom, y down, numbers kept to one decimal. The board is unbounded: the **tile grid** divides the world into 512-unit squares, tile `(tx, ty)` covering `[tx*512, (tx+1)*512)` on each axis. Tiles are how the board stays fast however far it grows:

- **The server** keeps each board's spatial index by tile (tile -> the elements whose bounds touch it). Hit tests, the eraser's candidates, `board_view`'s region, `board_read`'s region and the recognizer's neighbours only visit the tiles they cover.
- **The client** draws each layer as tiles: a tile is rendered once into an offscreen bitmap for the zoom level in use (levels are powers of two) and reused while panning; an edit invalidates only the tiles its element's old and new bounds touch. Thousands of strokes pan at the display's rate; the element being drawn and the selection are drawn live above the tiles.

## Layers

A board has ordered layers (bottom to top), each `{id, name, visible, locked, opacity}`, as in drawing programs: add, rename, reorder by drag, show or hide, lock, opacity, delete (with its elements), merge down. Every element is on one layer; drawing goes to the active layer; a locked layer takes no edit from the owner (the AI is told it is locked and may still change it when asked). Order within a layer is the elements' order (Bring to Front, Send to Back).

The convention the AI follows (`skills/board`): the owner's freehand stays on its layer; a cleaned-up diagram goes on a layer of its own ("Diagram") and the sketch layer is hidden, not deleted - nothing the owner drew is lost, and showing the sketch again compares the two.

## Elements

| type | fields | notes |
|---|---|---|
| `stroke` | `pts` (x, y pairs), `width`, `color`, `opacity` | freehand ink; the highlighter is a wide stroke at opacity 0.35 |
| `rect`, `round`, `ellipse`, `diamond`, `cylinder`, `hexagon`, `parallelogram` | `x, y, w, h`, `color`, `fill`, `width`, `style`, `text`, `fontSize` | **nodes**: the shapes a diagram is made of; `text` is the label, centered |
| `text` | `x, y, w, h`, `text`, `fontSize`, `color` | free text; `w, h` its measured box |
| `note` | `x, y, w, h`, `text`, `fill` | a sticky note: wrapped text on a colored square |
| `frame` | `x, y, w, h`, `text` (its title) | a container: the elements inside it are its members (a Mermaid `subgraph`) |
| `line`, `arrow` | `pts`, `start`, `end` (`none`, `arrow`, `dot`) | free lines and arrows, bound to nothing |
| `connector` | `from`, `to` (element ids), `text` (label), `start`, `end`, `route` (`straight`, `elbow`) | an edge between two elements; its ends follow them when they move |

Every element has `id` (`e<n>`, short so the AI can say it), `layer`, `type`, and optionally `color` and `fill` (palette names: `ink`, `gray`, `red`, `orange`, `amber`, `yellow`, `lime`, `green`, `teal`, `cyan`, `blue`, `indigo`, `violet`, `pink`; `fill` also `none`), `style` (`solid`, `dashed`, `dotted`), `key` (its Mermaid id, kept across export and import), `desc` (a longer description: what the component does, shown in its tooltip and in the plan), and `by` (`owner` or `ai`, who made it). Colors are names, never values: the client draws them with the theme's `--hue-*` tokens (`docs/07`) and the server's renderer reads the same tokens from `frontend/css/theme.css`.

## Changes, undo and the server's ownership

The server owns every open board (`AGENTS.md` rule 2): the browser and the AI change it through the same operations, each change is applied in order and pushed to every client on the board's topic, and the file is written a second after the last change (and when the board closes). A file changed on disk by something else (git, an editor) is read again and pushed whole.

A **change** is a list of operations applied at once (one undo step):

| op | fields |
|---|---|
| `add` | the element's fields at the op's top level; `ref` names it for later ops of the same change (`"@api"`) |
| `update` | `id` and the fields to set (`null` removes one) |
| `delete` | `ids` |
| `move` | `ids`, `dx`, `dy` (a frame moves its members) |
| `order` | `ids`, `to`: `front`, `back`, `forward`, `backward` |
| `add_layer` | `name`, `ref`; `above` (a layer id) |
| `update_layer` | `id`, `name`, `visible`, `locked`, `opacity`, `index` |
| `delete_layer` | `id` (its elements go with it) |
| `merge_layer` | `id` (into the layer below) |
| `layout` | `ids` (default: every node of `layer`), `direction` (`TD`, `LR`), `gap`: the nodes and their connectors laid out in layers |
| `align` | `ids`, `edge`: `left`, `right`, `top`, `bottom`, `centerx`, `centery` |
| `distribute` | `ids`, `axis`: `x`, `y` |
| `title` | `title` |

Every change records its inverse; `undo` takes back the latest change of its author that is not undone (the owner's Ctrl+Z never undoes the AI's work under them), `redo` repeats it, and `board_revert` / the History panel's Revert take back one named change. History is kept in memory for the board's life (the last 500 changes).

The client applies its own operations at once and keeps them pending until the server's event for them arrives (`opId`); new elements get temporary ids that the event maps to the board's.

## Protocol

Methods (`docs/10`; a project plugin's names are `ied.board/<method>`):

| Method | Params | Result |
|---|---|---|
| `ied.board/list` | `wsId` | `{boards: [{path, title, elements, layers, plans, modifiedMs}]}` |
| `ied.board/create` | `wsId, path, title?` | `{boardId, path}`; `conflict` when the file exists |
| `ied.board/open` | `wsId, path` | `{boardId}` (loads it; the topic has its state) |
| `ied.board/edit` | `boardId, ops, opId?, label?` | `{rev, change, refs: {ref: id}, warnings}` |
| `ied.board/undo`, `ied.board/redo` | `boardId` | `{rev, change}` or `{nothing: true}` |
| `ied.board/revert` | `boardId, change` | `{rev, change}` |
| `ied.board/plan` | `boardId, action: set \| delete, id?, title?, kind?, text?` | `{plans}` |
| `ied.board/export` | `boardId, format: mermaid \| markdown, ids?, layer?` | `{text, notes}` |
| `ied.board/import` | `boardId, format: mermaid, text, layer?, mode: merge \| replace` | `{rev, change, notes}` |
| `ied.board/recognize` | `boardId, ids?, region?` | `{proposals, ops}` |
| `ied.board/local` | `boardId, chatId?, camera?` | the board's per-machine state |

Topics: `ied.board/ws:<wsId>` - the workspace's boards (`boards.changed`); `ied.board/b:<boardId>` - snapshot `{rev, board, history}`, events `changed {rev, change: {id, by, label}, put: [element], removed: [id], layers?, title?, refs?, opId?}`, `plans {plans}`, `reloaded` (a new snapshot follows), `history {history}`.

## The board in the UI

A board opens as a document tab (`board:<path>`, from the Solution Explorer - double click on a `.iedboard`, or Open Board - from the Boards tool window, or New Board). Its parts, Visual Studio-dense:

- **The toolbar** (top): Select (V), Hand (H), Pen (P), Highlighter (I), Eraser (E; partial or whole strokes, its size), Rectangle (R), Ellipse (O), Diamond (D), Line (L), Arrow (A), Connector (C), Text (T), Note (N), Frame (F); the color palette and stroke width; Undo, Redo; zoom (fit, 100%, in, out).
- **The canvas**: pan with the Hand, space-drag, the middle button or two fingers; zoom with the wheel (Ctrl or not) or a pinch; a dot grid that follows the zoom. Select by click or box; drag to move, handles to resize, Delete, Ctrl+D duplicate, Ctrl+C/V, arrow keys nudge; double click a node, note, frame, connector or text to type its text in place; a connector is drawn from one element to another and binds both.
- **The side panel** (right; a pane of its own on a phone): **Layers** (the list, active layer, eye, lock, opacity, drag to reorder, add, delete, merge down), **Plan** (the board's plans: Mermaid and Markdown, edited in place; Apply to Board, Generate from Board, Copy, Save as File), **AI** (the board's chat and its actions), **History** (each change with who made it, Undo/Revert).
- **The AI section**: Ask (opens the board's chat beside the board, tethered, a new one the first time, `chat.create` with the board named), and actions that send it a message naming the board and the selection: **Clean up** (the selection, or everything), **To Mermaid**, **Write the plan**, **Review the design**, and **Build** - a new Agent chat given the plan to implement (asked first). The AI's edits arrive live; elements it changed flash, and History marks them `ai`.

## AI tools

The tools are designed around what a model is good and bad at, measured (see "Agent evaluation" below): it reads structure well, it reads pictures well, it places things by coordinates poorly, and every round trip costs. So: every element has a short id the model can say, every picture labels the ids it shows and carries a coordinate grid, the text listing groups the owner's strokes into named sketch groups with a guess at what each one is, recognition and layout are done by the tools rather than by the model's arithmetic, and one call can make many changes.

| Tool | Class | Does |
|---|---|---|
| `board_list` | read | the workspace's boards: path, title, layers, counts, plans |
| `board_create` | write | a new board file |
| `board_read` | read | the board as text: layers; the diagram (nodes with keys, text, geometry; connectors; frames and their members); the sketch as groups `g<n>` (the strokes near each other) with what each looks like and what is inside it; text and notes; plans. `region`, `layer`, `ids` narrow it |
| `board_view` | read | a picture (PNG) of the board or a `region` / around `ids`, with each element's or sketch group's id beside it and a labeled coordinate grid; `layers` picks the layers drawn |
| `board_recognize` | read | turns strokes into proposals: rectangles, ellipses, diamonds, lines, arrows (and which shapes they connect), and handwriting (strokes too small and curly to be shapes), each with a fit and the ready ops that would replace them; with `apply` and `labels` (`{"g1": "API Gateway"}`, read off the picture) it applies them, placing the clean elements on a `Diagram` layer and hiding the sketch layer (`sketch`: hide, delete, keep) |
| `board_edit` | write | a change: any of the ops above, refs between them, one undo step; says what it did and warns about what it could not |
| `board_export` | read | the diagram as a Mermaid flowchart (nodes by shape, labels, edge styles, frames as subgraphs) or as a Markdown outline (components with their descriptions and notes, connections, open notes); names what it could not include (strokes not yet recognized) |
| `board_import` | write | a Mermaid flowchart onto the board: nodes matched to existing ones by key, then by text (kept where they are), new ones laid out; `mode: replace` lays everything out again |
| `board_plan` | write | the board's plans: `read` (all, or one), `write` (Markdown or Mermaid text by id or a new one), `delete` |
| `board_revert` | write | takes back one change by its id (as `board_edit` named it) |

All of them name the board by its workspace path (`design/checkout.iedboard`) and work with no browser open. The AI's changes are `by: ai`. A system-message section tells the model the board tools exist and to read the `board` skill, which holds the workflows: cleaning up a sketch, converting to Mermaid and iterating, writing the plan, and handing it to an agent.

The binding `board.query {wsId, tool, args} -> {text, image?}` answers every tool, so other plugins, commands and the evaluation harness (`plugins/board/eval`) reach the same code.

## Rendering for the AI

`board_view` draws on the server, with no browser: a software rasterizer in the plugin's code (anti-aliased capsules for strokes and outlines, polygon fill for shapes and arrow heads, the Hershey simplex stroke font for text, the theme's colors) into an RGBA image, encoded with Bedrock's PNG writer into `.ied/local/boards/views/`. The picture is at most `max_size` pixels on its long side (default 1280); a region too large to read at that size says how far it was scaled and to look closer with `region`. The grid's labels are world coordinates, the same numbers `board_read` prints and `board_edit` takes.

## Recognition

`board_recognize` and the sketch groups of `board_read` share one recognizer (`code/core/Recognize.cf`):

1. Strokes are simplified (Ramer-Douglas-Peucker at 2% of their size) and measured: length, bounds, closure (end near start), straightness, turning.
2. Open, nearly straight strokes whose ends meet are joined into polylines, so a box drawn as four lines is one candidate.
3. A closed candidate is a rectangle (four corners near right angles, hull filling its bounds), a diamond (four corners near the bounds' edge midpoints), an ellipse (smooth, its points near the fitted ellipse) or a polygon.
4. A straight open stroke is a line; it is an arrow when one or two short strokes (or its own hook) make a V at an end. Its ends near a recognized shape's outline make it a connector between them.
5. What is left - short, curly strokes close together - is handwriting, grouped into lines by proximity; inside a shape, it is that shape's label.

## Mermaid

Export writes `flowchart TD` (or `LR` when the diagram is wider than tall): each node as `key[label]` in its shape's syntax (`(...)` round, `{...}` diamond, `((...))` ellipse, `[(...)]` cylinder, `{{...}}` hexagon, `[/.../]` parallelogram), each connector as `-->`, `---`, `-.->` (dashed), `==>` (wide), with `|label|`, frames as `subgraph key [title] ... end` around their members. A node without a key gets one from its text (`api_gateway`), the same every time.

Import reads `flowchart` / `graph` with a direction, nodes in those shapes (quoted labels too), edges (`-->`, `---`, `-.->`, `==>`, `<-->`, `--text-->`, `-->|text|`, chains `a --> b --> c`, `a & b --> c`), `subgraph ... end`, `%%` comments; `classDef`, `style`, `click` and `linkStyle` are left out with a note. It lays new nodes out in layers (longest path from the sources, barycenter ordering, the direction's axis) and makes frames around subgraphs.

## Agent evaluation

The tools were tuned by having agents use them (`plugins/board/eval`): a harness that starts a server on a scratch workspace, draws sketch boards from scripts (boxes, arrows and handwriting made of the Hershey script font's strokes, jittered like a hand), and runs the board tools through `board.query` from a command line an agent can call. Tasks: clean up a sketch into a diagram; convert a sketch to Mermaid; change a diagram from new instructions (add a cache between two services, rename, regroup); import Mermaid and keep the owner's positions; write a plan. Each run is judged on the final board (picture and elements) and the Mermaid against what the sketch meant. What the runs taught, and what changed because of it, is in "Evaluation results" below.

## Evaluation results

(Filled in from the runs.)

## Open items

- Images on the board (pasted screenshots) and links to code (`file:line` on a node).
- Other Mermaid diagram kinds (sequence, class, state) as plans; the board draws flowcharts.
- Live collaboration cursors from other devices.
