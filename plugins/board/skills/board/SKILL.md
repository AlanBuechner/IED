---
name: board
description: Work on the owner's planning boards (.iedboard) - read their freehand sketch, turn it into a clean diagram, draw architecture, data flow, timelines, state machines and memory layouts the owner can read zoomed out, convert to and from Mermaid, change a design on request, write the plan, and build from it - with the board_* tools.
keywords: [board, planning board, whiteboard, sketch, drawing, diagram, architecture, design, mermaid, flowchart, plan, frame, layer, recognize, clean up, tidy, layout, memory layout, struct layout, timeline, sequence diagram, state machine, data flow, readable, zoom]
---

# Planning boards

A board is the owner's design surface: an infinite canvas of layers holding freehand ink, shapes, connectors, text,
sticky notes, frames (boxes that group what is inside them) and images, plus **plans** - Markdown or Mermaid text kept
with the drawing. The owner draws in the board's tab; you work through the `board_*` tools, by the board's workspace
path. Both of you change the same board live: your changes appear on the owner's screen as you make them, marked as
yours in the History panel, and each `board_edit` / `board_recognize` / `board_import` is one change the owner can undo
and you can take back with `board_revert`.

## Seeing the board

- `board_view` is the picture: the owner's handwriting, what a sketch means, whether your change looks right. Every
  element's id (`e12`) and every group of strokes (`g3`) is tagged; the margins' numbers are board coordinates (x right,
  y down; positions are the top left of a box). Look closer with `region` or `ids` when text is small.
- `board_read` is the same as text: nodes with their keys, labels, positions and sizes; connectors; frames and what
  they hold; notes; and the owner's strokes grouped into sketch groups with a guess each (rectangle, ellipse, diamond,
  frame, arrow and what its ends touch, handwriting and what holds it). It does not read handwriting: you do, in the
  picture.
- Start with both: the picture for meaning, the text for ids and numbers.

## Cleaning up a sketch

1. `board_view` and `board_read`. Read every piece of handwriting off the picture.
2. `board_recognize` (no apply): the proposal says what each group would become. Check it against the picture: a big
   box drawn around other shapes is a **frame** (a group), an ellipse labeled "DB" is better a `cylinder`, an arrow's
   ends may be wrong.
3. `board_recognize` with `apply: true`, `labels` for every handwriting group you read (`{"g3": "API Gateway"}` - the
   shape that holds it gets it), and corrections in `types` (`{"g5": "frame", "g15": "cylinder", "g12": "note"}`) and
   `ends` (`{"g9": ["g4", "g7"]}`). A one-stroke arrow guessed a scribble takes `{"g21": "arrow"}`. Clean elements go
   on a "Diagram" layer and the sketch layer is hidden (when it also holds shapes, the recognized strokes move to a
   hidden "Sketch" layer and the shapes stay seen): every stroke kept, the owner shows the layer again to compare - never
   delete the owner's drawing. `place: "beside"` puts the clean copy to the right of the sketch instead.
4. Tidy: `board_edit` with `{"op": "tidy"}` keeps the owner's arrangement and lines up near rows and columns;
   `{"op": "layout", "direction": "TD"}` arranges everything anew by the connectors (frames stay together, notes move
   with their node). Prefer tidy when the owner placed things on purpose.
5. `board_view` to check. Then tell the owner what you made and what you guessed.

Keep the owner's words: labels as written (fix only obvious slips), their shapes unless asked. Say what you changed on
your own judgment (an ellipse made a cylinder, a note's wording).

## Changing a design

- One `board_edit` call with every op of the change; `ref` names new elements so later ops in the call can use them:
  `{"op": "add", "ref": "cache", "type": "cylinder", "text": "Redis", "color": "blue"}` then
  `{"op": "add", "type": "connector", "from": "e3", "to": "@cache", "text": "reads"}`.
- Put things where they belong without arithmetic: `place` (`{"op": "place", "ids": ["@cache"], "near": "e3", "side":
  "below"}`), `align` (to the selection or `to` an element), `distribute`, `layout` with `keep` (the nodes that stay)
  so only new nodes are placed around the owner's arrangement.
- Groups are frames: `{"op": "add", "type": "frame", "text": "Backend", "around": ["e3", "e4", "e6"]}` (anything else
  inside is moved out). A frame holds what on its own layer has its center inside it: `place` something outside to take
  it out (moves never grow a frame; `layout` and `tidy` refit them, `update` with `around` or `fit: true` refits one).
  The answer says when a change moved something in or out. Frames nest: a frame named in `around` brings what it holds
  (`{"op": "add", "type": "frame", "text": "Runtime", "around": ["e12", "e20"]}` with `e12` a frame keeps its members
  in it).
- A node between two others: `insert` on their connector (it makes room), or `place` with `between`.
- Read the answer's warnings (overlaps, connectors through nodes, frame membership) and fix them in the next call.
- `color` is the outline and ink, `fill` a tint inside; both take the palette's names. A note is tinted yellow unless
  told otherwise: its `color` is its outline and, when it has no `fill`, its tint too (`{"type": "note", "color":
  "red"}` is a red note); its text stays ink.

## Readable at every zoom

The owner reads a board two ways: zoomed out, to see its shape and find their way, and zoomed in, to read one part.
Draw for both.

- **Check it as the owner first sees it**: `board_view` of the whole board, no `region`. A label you cannot read in
  that picture the owner cannot read zoomed out either: shorten it, enlarge it, or move the detail into a note or `desc`.
- **Short labels**: a node's label is its name, 1-4 words ("Asset cache", not "Caches decoded assets between frames").
  What it does goes in its `desc` (it reaches the export and the plan) or in a note `about` it.
- **One size per role**, set with `fontSize` and kept the same across the board: the board's title 40 (a `text` at the
  top left), a heading over a group of frames 28, node labels 16-18 (16 is the default; 20 for the few that matter
  most), notes and annotations 14, never below 12. Mixed sizes for one role read as noise.
- **Frames are the map**: group with frames, each titled in 1-3 words. Zoomed out, frame titles are what the owner can
  still read - the canvas names every frame on the screen just above it - so leave about 60 units clear above a frame
  and nest frames at most two deep.
- **Room**: 60-120 between nodes (120 when the connector between them has a label), 100 or more between frames. Line
  things up (`tidy`, `align`, `distribute`) so the eye can follow rows and columns.
- **One reading direction** per diagram - left to right for flows and pipelines, top down for layers and hierarchies -
  with the main path straight and side paths off it. Number the steps when order matters ("1 Load", "2 Decode").
- **Color means something**: one hue per kind, the same across the board (blue for the program's own code, green for
  storage, orange for external systems, red for problems and hot paths), a legend note when you use more than two, and
  `fill` only for the few elements that matter most.
- **Connectors** carry a short label of what crosses them ("draw calls", "16 KB/frame", "owns"); leave the obvious ones
  bare. `style: "dashed"` for asynchronous, optional or weak links.
- **Big boards**: several diagrams side by side as frames, in reading order under headings, the overview at the top
  left and each closer look to its right or below it, with a note at the top left saying what the board shows and
  where to start.

## Kinds of diagrams

Draw the picture that answers the owner's question, and let its frame's title say what it shows ("Frame loop",
"Transform: 48 bytes"). One level of detail per diagram: for the next level, a frame of its own beside it, named after
what it opens up ("Renderer, inside").

- **Architecture**: components as boxes, boundaries (process, thread, module, machine) as frames, stores as `cylinder`,
  external systems and hardware APIs as `hexagon`, data and messages as `parallelogram`. Layers top down (tools and UI,
  systems, engine core, platform) with dependencies pointing down; connectors say what crosses (calls, events, data).
- **Data flow and pipelines**: stages left to right, with the data between them as a `parallelogram` or a connector
  label giving its shape and size ("vertices, 32 B each"). Frames mark where it changes thread or goes to the GPU.
- **Sequence and timelines**: a column per actor or thread (a header node with a dashed `line` down), time going down,
  numbered arrows between columns. For timing within a frame, a horizontal timeline instead: one row per thread,
  blocks whose widths are proportional to time (a note gives the scale: "100 units = 1 ms"), sync points as diamonds.
- **State machines**: `round` states, connectors labeled "event / action", the start state marked with a `fill`.
- **Memory layouts**: a row of `rect` cells edge to edge (no gap), widths proportional to bytes at one scale for the
  whole picture - about 16 units per byte, so a cell holds its name; a field too narrow for its name gets it above it.
  Label a cell "name: type", put its offset over it as a `text` of fontSize 12 (`0x00`, `0x08`), draw padding as
  dashed gray cells labeled "pad", cache line (64 B) boundaries as dashed vertical `line`s, and the size and alignment
  at the row's end ("48 B, align 16"). An array: its first cells, a "..." cell, its last. A pointer: a connector from
  its cell to the row it points at. Compare layouts (array of structs against struct of arrays, before and after
  reordering fields) as rows one above another at the same scale and left edge, so the difference shows. Give every
  cell its `x`, `y`, `w` and `h`: here arithmetic beats `place`.
- **Tables** (an archetype's component columns, a lookup table, a packet header): a frame holding rows of edge-to-edge
  rects, the header row with a `fill`. Bit fields are memory layouts at a scale per bit.
- **Ownership and lifetime**: a solid connector for owns, dashed for borrows or refers to, frames for arenas and
  allocators, a note saying when each is freed.
- **Concurrency**: a frame per thread or job, queues between them as `cylinder`s, locks and fences as diamonds,
  connectors labeled with what is handed over.

Explain as you draw: a short note `about` each surprising or important point ("8 B of padding here: reorder to save
it"), and a summary note at the top left. Then `board_view` the whole board and each part, and tell the owner what each
picture shows.

## Mermaid both ways

- `board_export` gives a `flowchart` of the diagram (nodes, connectors, frames as subgraphs, colors as `class` lines,
  notes as comments). It is stable: keys come from the board, the order does not follow positions.
- To change the design in text, edit that Mermaid and `board_import` it (merge): nodes it already has keep their
  places, new ones are placed beside them, connectors become the text's. `dry_run: true` first when the change is big.
  `mode: "replace"` only when the owner wants it laid out anew.
- Keep a Mermaid plan current: `board_plan read` says when the board's diagram changed since the plan was written.

## The plan

The plan is what an agent builds from. Write it with `board_plan` (`action: "write"`, `kind: "markdown"`), starting from
`board_export` with `format: "markdown"`: one section per component (what it does, its interface, what it talks to and
how), the flows, the data, open questions - and the Mermaid diagram. Give nodes and frames a `desc` with `board_edit` as you
learn what they are for: descriptions flow into the export. Put `{{board:mermaid}}` where the diagram goes rather than
pasting it: it is filled in from the board each time the plan is read, so it never drifts. Pictures drawn cell by cell (a
memory layout, a table) are boxes too: put them on a layer of their own and write `{{board:mermaid:<diagram layer>}}`, so
the plan's diagram holds the design and not every cell. Mark assumptions as
assumptions. `board_read`'s checks list loose ends worth raising (a decision with one way out).

## Building

When the owner asks to build the design: read the plan (`board_plan read`) and the diagram (`board_export`), then
implement it in the workspace as any coding task - or, when the owner asks for a separate agent, start one with
`chat_create` and a message naming the board and its plan. Keep the board in step: mark what is done (a `desc`, a
green `color`) only when the owner wants that.
