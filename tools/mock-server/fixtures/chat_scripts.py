"""Scripted assistant turns for the mock chat (docs/10 "Chats").

Ops: ("thinking", text) | ("text", text) | ("tool", spec) | ("ask", question) | ("tasks", items) | ("harness", kind, text)
| ("context", data) | ("pause", seconds) | ("end", stop, extra).
A tool spec has name, args, result {preview, isError?}, ms, and optionally approval {summary, why, subject, diff} (waits for
chat.approve), plus / minus (a write's lines) and path (the file it changed). A question has question, options, allowText.
"""

CONTEXT = {"window": 32768, "used": 18240, "parts": [
	{"name": "System prompt", "tokens": 4100}, {"name": "Tool definitions", "tokens": 1600},
	{"name": "Messages", "tokens": 9800}, {"name": "Tool results", "tokens": 2740}]}

DEFAULT = [
	("thinking", "The user wants to know how the player moves. Let me look at the project structure first, then read the player update."),
	("tasks", [
		{"id": "t1", "title": "Read the player update", "status": "in_progress"},
		{"id": "t2", "title": "Explain the integration step", "status": "pending"},
	]),
	("tool", {"name": "fs_list", "args": {"path": "src"}, "result": {"preview": "src/main.c\nsrc/game/player.c\nsrc/game/player.h\nsrc/engine/render.c\nsrc/engine/render.h\nsrc/engine/math.h"}, "ms": 12}),
	("tool", {"name": "fs_read", "args": {"path": "src/game/player.c", "offset": 1, "limit": 12}, "result": {"preview": "src/game/player.c (12 lines)"}, "ms": 7}),
	("context", CONTEXT),
	("text", (
		"The player moves with explicit Euler integration in `player_update` (src/game/player.c):\n\n"
		"```c\nplayer->position = vec2_add(player->position, vec2_scale(player->velocity, dt));\n```\n\n"
		"- `main` calls it once per frame with a fixed `dt` of 1/60 s.\n"
		"- `velocity` starts at (1, 0), so the player drifts right at one unit per second.\n"
		"- There is no acceleration or clamping yet; adding input would mean updating `velocity` before this line.\n"
	)),
	("tasks", [
		{"id": "t1", "title": "Read the player update", "status": "done"},
		{"id": "t2", "title": "Explain the integration step", "status": "done"},
	]),
	("end", "finished", {}),
]

DIFF = (
	"@@ -1,5 +1,6 @@\n"
	" void player_update(Player* player, float dt)\n"
	" {\n"
	"+\tplayer->velocity = vec2_clamp(player->velocity, MAX_SPEED);\n"
	" \tplayer->position = vec2_add(player->position, vec2_scale(player->velocity, dt));\n"
	" }\n"
)

APPROVAL = [
	("thinking", "Clamping the velocity needs an edit to player.c; edits need the owner's approval in this chat."),
	("text", "I'll clamp the velocity before integrating it:\n"),
	("tool", {"name": "fs_edit", "args": {"path": "src/game/player.c", "edits": [{"old": "{\n", "new": "{\n\tplayer->velocity = vec2_clamp(player->velocity, MAX_SPEED);\n"}]},
		"approval": {"summary": "Edit src/game/player.c", "why": "agent mode, permission \"ask\" asks for edits; no rule matched", "subject": "fs.write(src/game/**)", "diff": DIFF, "plus": 1, "minus": 0},
		"result": {"preview": DIFF}, "ms": 6, "plus": 1, "minus": 0, "path": "src/game/player.c"}),
	("tool", {"name": "shell_run", "args": {"command": "nuke test dev"}, "result": {"preview": "exit 1\n2 passed, 1 failed (test_player_update)", "isError": True}, "ms": 2140, "source": "Output: AI"}),
	("harness", "continue_after_max_tokens", "Your reply was cut off by the output limit. Continue exactly where you stopped - do not repeat what you already wrote."),
	("text", "One test fails: `test_player_update` expects the position after one second, but the test only advanced one frame.\n"),
	("end", "finished", {"recoveries": ["continued after max_tokens (1x)"]}),
]

ASK = [
	("text", "Before I change the movement I need one decision:\n"),
	("ask", {"question": "Should the player keep its speed when it stops pressing a key?", "options": ["Keep it", "Slow down"], "allowText": True}),
	("text", "Understood. I'll leave the speed as it is.\n"),
	("end", "finished", {}),
]

ERROR = [
	("text", "Let me start by reading the renderer so I can"),
	("pause", 0.5),
	("harness", "paused", "Paused (http_error 500): http_error 500 after retries: model server returned HTTP 500. Send a message to continue."),
	("end", "http_error 500", {"recoveries": ["retried after http_error 500 (1x)", "retried after http_error 500 (2x)", "retried after http_error 500 (3x)"], "paused": True}),
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

SNAP = [
	("tool", {"name": "game_snap", "args": {}, "result": {"preview": "the game's screen, 320x180\n[image shots/screen.png]", "images": [{"mime": "image/png", "path": "shots/screen.png"}]},
		"ms": 41, "source": "plugin game.snap"}),
	("text", "The screen shows a red rectangle at the top left and a blue circle on the right.\n"),
	("end", "finished", {}),
]

SCRIPTS = {"default": DEFAULT, "approval": APPROVAL, "ask": ASK, "error": ERROR, "long": LONG, "snap": SNAP}
KEYWORDS = [("screen", "snap"), ("approv", "approval"), ("clamp", "approval"), ("run ", "approval"), ("test", "approval"), ("ask me", "ask"), ("decide", "ask"),
	("long", "long"), ("essay", "long"), ("error", "error"), ("fail", "error")]

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
