"""Scripted assistant turns for the mock chat (docs/10 chat streaming).

Ops: ("thinking", text) | ("text", text) | ("tool", spec) | ("tasks", items) | ("pause", seconds) | ("end", stop, extra).
A tool spec has name, args, result, ms, and optionally approval=True (waits for chat.approve).
"""

DEFAULT = [
	("thinking", "The user wants to know how the player moves. Let me look at the project structure first, then read the player update."),
	("tool", {"name": "list_files", "args": {"path": "src"}, "result": {"preview": "src/main.c\nsrc/game/player.c\nsrc/game/player.h\nsrc/engine/render.c\nsrc/engine/render.h\nsrc/engine/math.h"}, "ms": 12}),
	("tool", {"name": "read_file", "args": {"path": "src/game/player.c", "startLine": 1, "endLine": 12}, "result": {"preview": "void player_update(Player* player, float dt)\n{\n\tplayer->position = vec2_add(...);\n}"}, "ms": 7}),
	("tasks", [
		{"id": "1", "text": "Read the player update", "status": "done"},
		{"id": "2", "text": "Explain the integration step", "status": "in_progress"},
	]),
	("text", (
		"The player moves with explicit Euler integration in `player_update` (src/game/player.c):\n\n"
		"```c\nplayer->position = vec2_add(player->position, vec2_scale(player->velocity, dt));\n```\n\n"
		"- `main` calls it once per frame with a fixed `dt` of 1/60 s.\n"
		"- `velocity` starts at (1, 0), so the player drifts right at one unit per second.\n"
		"- There is no acceleration or clamping yet; adding input would mean updating `velocity` before this line.\n"
	)),
	("tasks", [
		{"id": "1", "text": "Read the player update", "status": "done"},
		{"id": "2", "text": "Explain the integration step", "status": "done"},
	]),
	("end", "finished", {}),
]

APPROVAL = [
	("thinking", "To check the change I should run the tests. Running a shell command needs the owner's approval."),
	("tool", {"name": "run_shell", "args": {"command": "nuke test dev"}, "approval": True, "result": {"preview": "2 passed, 1 failed (test_player_update)"}, "ms": 2140}),
	("text", "One test fails: `test_player_update` expects the position after one second, but the test only advanced one frame.\n"),
	("end", "finished", {}),
]

ERROR = [
	("text", "Let me start by reading the renderer so I can"),
	("pause", 0.5),
	("end", "error", {"error": {"reason": "http_error", "status": 500, "message": "model server returned HTTP 500 after 3 retries"}}),
]

_LONG_PARAGRAPH = (
	"The render loop owns the frame: it polls the window, advances the simulation by a fixed step, "
	"and presents. Keeping the step fixed makes the player integration deterministic, which matters "
	"for replays and for tests that compare positions after a known number of frames. "
)

LONG = [
	("thinking", "This needs a long explanation; streaming it in many small deltas."),
	("text", "".join("%d. %s\n\n" % (i, _LONG_PARAGRAPH) for i in range(1, 41))),
	("end", "finished", {}),
]

SCRIPTS = {"default": DEFAULT, "approval": APPROVAL, "error": ERROR, "long": LONG}
KEYWORDS = [("approv", "approval"), ("run ", "approval"), ("test", "approval"), ("long", "long"), ("essay", "long"), ("error", "error"), ("fail", "error")]

SEED_TURNS = [
	("Where is the entry point?", "The entry point is `main` in src/main.c. It creates the player and runs the render loop until `render_frame` returns 0."),
	("How many frames does it run?", "`render_frame` returns 0 after 600 frames, so the demo runs for ten seconds at 60 fps."),
]


def pick(text, requested):
	if requested:
		return requested
	lowered = text.lower()
	for keyword, name in KEYWORDS:
		if keyword in lowered:
			return name
	return "default"
