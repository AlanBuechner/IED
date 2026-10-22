"""The fake workspace: a small, language-neutral C project (tree, documents, diagnostics targets) and an image a tool
shows."""

import struct
import zlib

WORKSPACE = {"wsId": "w1", "name": "demo", "root": "C:/mock/demo"}


def solution_files(name):
	"""A recent solution's files when it is opened again: the solution, two programs (src/, viewer/) and a library (core/),
	their code and README."""
	return {
		"README.md": "# %s\n" % name,
		name + ".iedsln": '{ "version": 1, "name": "%s", "projects": [{ "file": "src/Tool.iedproj" }, { "file": "viewer/Viewer.iedproj" }, { "file": "core/Core.iedproj" }], "items": ["README.md"] }\n' % name,
		"src/Tool.iedproj": '{ "version": 1, "name": "Tool", "root": ".", "kind": "executable" }\n',
		"src/tool.c": "int main(void) { return 0; }\n",
		"viewer/Viewer.iedproj": '{ "version": 1, "name": "Viewer", "root": ".", "kind": "executable" }\n',
		"viewer/viewer.c": "int main(void) { return 0; }\n",
		"core/Core.iedproj": '{ "version": 1, "name": "Core", "root": ".", "kind": "library" }\n',
		"core/core.c": "int core(void) { return 1; }\n",
	}


def project(root, folder, name, kind):
	return {"name": name, "file": "%s/%s/%s.iedproj" % (root, folder, name), "root": root + "/" + folder, "kind": kind, "problems": [], "data": {"version": 1, "name": name, "root": ".", "kind": kind}}


def solution_model(name, root):
	"""What the server's ws:<wsId> snapshot carries as `solution` for solution_files (docs/03 "The server as built")."""
	return {
		"name": name, "file": root + "/" + name + ".iedsln", "problems": [], "groups": [], "items": ["README.md"], "generator": None, "localPlugins": [],
		"projects": [project(root, "src", "Tool", "executable"), project(root, "viewer", "Viewer", "executable"), project(root, "core", "Core", "library")],
	}


def recent_projects(now_ms):
	"""The server's recent list (docs/10 "Workspaces"), newest first: the open demo, a solution, a favorite and a folder
	that is gone."""
	hour, day = 3600 * 1000, 24 * 3600 * 1000
	return [
		{"path": "C:/mock/demo", "name": "demo", "kind": "folder", "opened": now_ms - 5 * 60 * 1000, "favorite": False, "exists": True},
		{"path": "C:/mock/Tools/Tools.iedsln", "name": "Tools", "kind": "solution", "opened": now_ms - 3 * hour, "favorite": False, "exists": True},
		{"path": "C:/mock/Shooter", "name": "Shooter", "kind": "folder", "opened": now_ms - 2 * day, "favorite": True, "exists": True},
		{"path": "C:/mock/Prototype", "name": "Prototype", "kind": "folder", "opened": now_ms - 40 * day, "favorite": False, "exists": False},
	]

FILES = {
	"README.md": (
		"# demo\n\n"
		"A tiny fixture project served by the IED mock server.\n\n"
		"- `src/` game and engine sources\n"
		"- `tests/` unit tests\n"
		"- `docs/` design notes\n"
	),
	".gitignore": "build/\n*.obj\n",
	"src/main.c": (
		"#include \"engine/render.h\"\n"
		"#include \"game/player.h\"\n"
		"\n"
		"int main(int argc, char** argv)\n"
		"{\n"
		"\tPlayer player = player_create(\"hero\");\n"
		"\tRenderer* renderer = render_init(1280, 720);\n"
		"\twhile (render_frame(renderer))\n"
		"\t{\n"
		"\t\tplayer_update(&player, 1.0f / 60.0f);\n"
		"\t}\n"
		"\trender_shutdown(renderer);\n"
		"\treturn 0;\n"
		"}\n"
	),
	"src/game/player.h": (
		"#pragma once\n"
		"#include \"../engine/math.h\"\n"
		"\n"
		"typedef struct Player\n"
		"{\n"
		"\tconst char* name;\n"
		"\tVec2 position;\n"
		"\tVec2 velocity;\n"
		"} Player;\n"
		"\n"
		"Player player_create(const char* name);\n"
		"void player_update(Player* player, float dt);\n"
	),
	"src/game/player.c": (
		"#include \"player.h\"\n"
		"\n"
		"Player player_create(const char* name)\n"
		"{\n"
		"\tPlayer p = { name, { 0, 0 }, { 1, 0 } };\n"
		"\treturn p;\n"
		"}\n"
		"\n"
		"void player_update(Player* player, float dt)\n"
		"{\n"
		"\tplayer->position = vec2_add(player->position, vec2_scale(player->velocity, dt));\n"
		"}\n"
	),
	"src/engine/math.h": (
		"#pragma once\n"
		"\n"
		"typedef struct Vec2 { float x, y; } Vec2;\n"
		"\n"
		"static inline Vec2 vec2_add(Vec2 a, Vec2 b) { Vec2 r = { a.x + b.x, a.y + b.y }; return r; }\n"
		"static inline Vec2 vec2_scale(Vec2 a, float s) { Vec2 r = { a.x * s, a.y * s }; return r; }\n"
	),
	"src/engine/render.h": (
		"#pragma once\n"
		"\n"
		"typedef struct Renderer Renderer;\n"
		"\n"
		"Renderer* render_init(int width, int height);\n"
		"int render_frame(Renderer* renderer);\n"
		"void render_shutdown(Renderer* renderer);\n"
	),
	"src/engine/render.c": (
		"#include \"render.h\"\n"
		"#include <stdlib.h>\n"
		"\n"
		"struct Renderer { int width; int height; int frames; };\n"
		"\n"
		"Renderer* render_init(int width, int height)\n"
		"{\n"
		"\tRenderer* r = malloc(sizeof(Renderer));\n"
		"\tfloat scale = 1.0f;\n"
		"\tr->width = width;\n"
		"\tr->height = height;\n"
		"\tr->frames = 0;\n"
		"\treturn r;\n"
		"}\n"
		"\n"
		"int render_frame(Renderer* renderer)\n"
		"{\n"
		"\treturn ++renderer->frames < 600;\n"
		"}\n"
		"\n"
		"void render_shutdown(Renderer* renderer)\n"
		"{\n"
		"\tfree(renderer);\n"
		"}\n"
	),
	"tests/test_math.c": (
		"#include \"../src/engine/math.h\"\n"
		"#include <assert.h>\n"
		"\n"
		"void test_vec2_add(void)\n"
		"{\n"
		"\tVec2 r = vec2_add((Vec2){ 1, 2 }, (Vec2){ 3, 4 });\n"
		"\tassert(r.x == 4 && r.y == 6);\n"
		"}\n"
	),
	"docs/design.md": (
		"# Design notes\n\n"
		"Unicode check for column math: caf\u00e9, \u65e5\u672c, and an emoji \U0001F3AE (two UTF-16 units).\n"
	),
	"assets/config.json": "{\n\t\"window\": { \"width\": 1280, \"height\": 720 },\n\t\"vsync\": true\n}\n",
}

DIRECTORIES = ["scratch"]


def _png(width, height, pixel):
	"""An RGB PNG of `pixel(x, y)`."""
	rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

	def chunk(kind, data):
		return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
	return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def _screen(x, y):
	if 20 <= x < 140 and 30 <= y < 90:
		return (200, 60, 60)
	if (x - 230) ** 2 + (y - 110) ** 2 < 40 ** 2:
		return (60, 140, 220)
	return (30 + x * 40 // 320, 30 + y * 40 // 180, 50)


# Images served from /w/<wsId>/files/ (not in the tree: the mock's files are text).
IMAGES = {"shots/screen.png": _png(320, 180, _screen)}


def generated_files(count):
	return {"generated/file_%05d.txt" % i: "generated fixture file %d\n" % i for i in range(count)}
