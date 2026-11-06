---
name: board
description: Work on the owner's planning boards (.iedboard) - read their freehand sketch, turn it into a clean diagram, convert it to and from Mermaid, change a design on request, write the plan, and build from it - with the board_* tools.
keywords: [board, planning board, whiteboard, sketch, drawing, diagram, architecture, design, mermaid, flowchart, plan, frame, layer, recognize, clean up, tidy, layout]
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
   on a "Diagram" layer and the sketch layer is hidden: every stroke kept, the owner shows the layer again to compare -
   never delete the owner's drawing. `place: "beside"` puts the clean copy to the right of the sketch instead.
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
  The answer says when a change moved something in or out.
- A node between two others: `insert` on their connector (it makes room), or `place` with `between`.
- Read the answer's warnings (overlaps, connectors through nodes, frame membership) and fix them in the next call.
- `color` is the outline and ink, `fill` a tint inside; both take the palette's names.

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
pasting it: it is filled in from the board each time the plan is read, so it never drifts. Mark assumptions as
assumptions. `board_read`'s checks list loose ends worth raising (a decision with one way out).

## Building

When the owner asks to build the design: read the plan (`board_plan read`) and the diagram (`board_export`), then
implement it in the workspace as any coding task - or, when the owner asks for a separate agent, start one with
`chat_create` and a message naming the board and its plan. Keep the board in step: mark what is done (a `desc`, a
green `color`) only when the owner wants that.
