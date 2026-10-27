"""An MCP server (stdio, spec 2025-06-18) for Blender through the Blender Lab "MCP" add-on (lab.blender.org, Blender 5.1+).

The add-on only runs a bridge inside Blender: a TCP socket (localhost:9876 by default) that takes a null-terminated JSON
request {"type": "execute", "code": ..., "strict_json": ...}, runs the code on Blender's main thread and answers with
the `result` dict the code set. The MCP server half - the program an MCP client starts - comes separately from Blender
Lab; this one is a stdlib stand-in for it, so IED (docs/04 "MCP") can drive a Blender that has the add-on:

	"mcp.servers": {"blender": {"command": "python", "args": ["-I", "<repo>/tools/blender-lab-mcp/server.py"]}}

Blender must run with the add-on's server started (Preferences > Add-ons > MCP; it starts by itself when online access
is on - `blender --online-mode`). Tools: scene_info, create_object, run_python, viewport_image.
	python -I tools/blender-lab-mcp/server.py [--host localhost] [--port 9876] [--timeout 60]
"""

import argparse
import base64
import json
import os
import socket
import sys
import tempfile
import threading

PROTOCOL = "2025-06-18"

TOOLS = [
	{"name": "scene_info", "title": "Scene info",
		"description": "The open Blender scene: its file, frame, render engine and every object with its type, location, rotation, scale, dimensions and visibility.",
		"inputSchema": {"type": "object", "properties": {}}, "annotations": {"readOnlyHint": True}},
	{"name": "create_object", "title": "Create object",
		"description": "Add a mesh object to the scene: cube, sphere, cylinder, cone, plane, torus or monkey, at a location with a size; answers its name.",
		"inputSchema": {"type": "object", "properties": {
			"kind": {"enum": ["cube", "sphere", "cylinder", "cone", "plane", "torus", "monkey"]},
			"name": {"type": "string"},
			"location": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
			"size": {"type": "number", "minimum": 0},
			"color": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 4, "description": "RGB(A), 0 to 1: a material of that color."}},
			"required": ["kind"]}},
	{"name": "run_python", "title": "Run Python in Blender",
		"description": "Run Python code in Blender (bpy is imported); set `result` to a dict of what to answer. Its print output comes back too.",
		"inputSchema": {"type": "object", "properties": {"code": {"type": "string"}}, "required": ["code"]}},
	{"name": "viewport_image", "title": "Viewport image",
		"description": "A picture of Blender's 3D viewport as it is now: the view drawn without the interface over it (PNG, half the render resolution).",
		"inputSchema": {"type": "object", "properties": {}}, "annotations": {"readOnlyHint": True}},
]

SCENE_INFO = r'''
import bpy
scene = bpy.context.scene
objects = []
for o in scene.objects:
	objects.append({"name": o.name, "type": o.type, "location": [round(v, 4) for v in o.location],
		"rotation": [round(v, 4) for v in o.rotation_euler], "scale": [round(v, 4) for v in o.scale],
		"dimensions": [round(v, 4) for v in o.dimensions], "visible": o.visible_get()})
result = {"file": bpy.data.filepath or "(unsaved)", "scene": scene.name, "frame": scene.frame_current,
	"engine": scene.render.engine, "blender": bpy.app.version_string, "objects": objects}
'''

CREATE_OBJECT = r'''
import bpy, bmesh
args = %s
kind = args["kind"]
size = float(args.get("size", 2.0))
mesh = bpy.data.meshes.new(args.get("name") or kind.capitalize())
b = bmesh.new()
if kind == "cube":
	bmesh.ops.create_cube(b, size=size)
elif kind == "sphere":
	bmesh.ops.create_uvsphere(b, u_segments=32, v_segments=16, radius=size / 2)
elif kind == "cylinder":
	bmesh.ops.create_cone(b, cap_ends=True, segments=32, radius1=size / 2, radius2=size / 2, depth=size)
elif kind == "cone":
	bmesh.ops.create_cone(b, cap_ends=True, segments=32, radius1=size / 2, radius2=0, depth=size)
elif kind == "plane":
	bmesh.ops.create_grid(b, x_segments=1, y_segments=1, size=size / 2)
elif kind == "torus":
	import math
	major, minor = size / 2, size / 6
	verts = []
	for i in range(32):
		ring = []
		a = 2 * math.pi * i / 32
		for j in range(12):
			c = 2 * math.pi * j / 12
			r = major + minor * math.cos(c)
			ring.append(b.verts.new((r * math.cos(a), r * math.sin(a), minor * math.sin(c))))
		verts.append(ring)
	for i in range(32):
		for j in range(12):
			b.faces.new((verts[i][j], verts[(i + 1) %% 32][j], verts[(i + 1) %% 32][(j + 1) %% 12], verts[i][(j + 1) %% 12]))
else:
	bmesh.ops.create_monkey(b)
	bmesh.ops.scale(b, vec=(size / 2, size / 2, size / 2), verts=b.verts)
b.to_mesh(mesh)
b.free()
obj = bpy.data.objects.new(mesh.name, mesh)
obj.location = args.get("location", [0, 0, 0])
bpy.context.scene.collection.objects.link(obj)
if args.get("color"):
	color = list(args["color"]) + [1.0] * (4 - len(args["color"]))
	material = bpy.data.materials.new(obj.name + " color")
	material.diffuse_color = color
	obj.data.materials.append(material)
	obj.color = color
for area in bpy.context.screen.areas if bpy.context.screen else []:
	area.tag_redraw()
result = {"name": obj.name, "type": obj.type, "location": [round(v, 4) for v in obj.location], "vertices": len(mesh.vertices)}
'''

VIEWPORT_IMAGE = r'''
import bpy
path = %s
done = False
problems = []
render = bpy.context.scene.render
kept = render.resolution_percentage
render.resolution_percentage = 50
for window in bpy.context.window_manager.windows:
	for area in window.screen.areas:
		if area.type != "VIEW_3D" or done:
			continue
		region = [r for r in area.regions if r.type == "WINDOW"][0]
		for space in area.spaces:
			if space.type == "VIEW_3D":
				space.shading.color_type = "OBJECT"
		try:
			with bpy.context.temp_override(window=window, area=area, region=region):
				bpy.ops.render.opengl(view_context=True)
			bpy.data.images["Render Result"].save_render(path)
			done = True
		except Exception as e:
			problems.append(str(e))
render.resolution_percentage = kept
result = {"saved": done, "problems": problems}
'''


class Bridge:
	"""The add-on's socket: a request a connection, null-terminated JSON each way."""

	def __init__(self, host, port, timeout):
		self.host, self.port, self.timeout = host, port, timeout

	def execute(self, code, strict=True):
		with socket.create_connection((self.host, self.port), timeout=10) as s:
			s.settimeout(self.timeout)
			s.sendall((json.dumps({"type": "execute", "code": code, "strict_json": strict}) + "\0").encode())
			data = b""
			while b"\0" not in data:
				chunk = s.recv(65536)
				if not chunk:
					break
				data += chunk
		return json.loads(data.split(b"\0", 1)[0])


def text(value):
	return {"content": [{"type": "text", "text": value if isinstance(value, str) else json.dumps(value)}]}


def failure(message):
	return {"content": [{"type": "text", "text": message}], "isError": True}


def call(bridge, name, arguments):
	try:
		if name == "scene_info":
			answer = bridge.execute(SCENE_INFO)
		elif name == "create_object":
			answer = bridge.execute(CREATE_OBJECT % repr(arguments))
		elif name == "run_python":
			answer = bridge.execute(arguments.get("code", ""), strict=False)
		elif name == "viewport_image":
			path = os.path.join(tempfile.gettempdir(), "blender-lab-mcp-%d.png" % os.getpid())
			answer = bridge.execute(VIEWPORT_IMAGE % repr(path.replace("\\", "/")))
			if answer.get("status") == "ok" and answer["result"].get("saved") and os.path.exists(path):
				with open(path, "rb") as f:
					data = base64.b64encode(f.read()).decode()
				os.remove(path)
				return {"content": [{"type": "text", "text": "Blender's 3D viewport"}, {"type": "image", "mimeType": "image/png", "data": data}]}
			return failure("the viewport could not be captured: " + json.dumps(answer.get("result") or answer))
		else:
			return None
	except OSError as e:
		return failure("Blender's MCP add-on does not answer at %s:%d (%s): start Blender with the MCP add-on (Preferences > Add-ons > MCP, Start; online access on)" % (bridge.host, bridge.port, e))
	if answer.get("status") != "ok":
		return failure("Blender: " + str(answer.get("message", answer)) + ("\n" + answer["stdout"] if answer.get("stdout") else ""))
	result = answer.get("result", {})
	if answer.get("stdout"):
		result = dict(result, stdout=answer["stdout"])
	return text(result)


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--host", default=os.environ.get("BLENDER_MCP_HOST", "localhost"))
	parser.add_argument("--port", type=int, default=int(os.environ.get("BLENDER_MCP_PORT", "9876")))
	parser.add_argument("--timeout", type=float, default=60)
	args = parser.parse_args()
	bridge = Bridge(args.host, args.port, args.timeout)
	lock = threading.Lock()

	def send(message):
		with lock:
			sys.stdout.write(json.dumps(message) + "\n")
			sys.stdout.flush()

	def handle(message):
		method, rid = message.get("method"), message.get("id")
		if rid is None or method is None:
			return
		if method == "initialize":
			send({"jsonrpc": "2.0", "id": rid, "result": {"protocolVersion": PROTOCOL, "capabilities": {"tools": {}},
				"serverInfo": {"name": "blender-lab-mcp", "version": "0.1.0"},
				"instructions": "Blender through the Blender Lab MCP add-on: scene_info first, create_object for primitives, run_python for anything else (set result to a dict)."}})
		elif method == "ping":
			send({"jsonrpc": "2.0", "id": rid, "result": {}})
		elif method == "tools/list":
			send({"jsonrpc": "2.0", "id": rid, "result": {"tools": TOOLS}})
		elif method == "tools/call":
			params = message.get("params", {})
			answer = call(bridge, params.get("name"), params.get("arguments") or {})
			if answer is None:
				send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32602, "message": "no tool " + str(params.get("name"))}})
			else:
				send({"jsonrpc": "2.0", "id": rid, "result": answer})
		else:
			send({"jsonrpc": "2.0", "id": rid, "error": {"code": -32601, "message": "no method " + method}})

	sys.stderr.write("blender-lab-mcp: Blender's add-on at %s:%d\n" % (args.host, args.port))
	sys.stderr.flush()
	for line in sys.stdin:
		if line.strip():
			threading.Thread(target=handle, args=(json.loads(line),), daemon=True).start()


if __name__ == "__main__":
	main()
