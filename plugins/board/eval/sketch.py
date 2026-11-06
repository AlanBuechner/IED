"""sketch - hand-drawn boards for the evaluation (docs/32 "Agent evaluation").

Draws a scenario's boxes, ellipses, diamonds, arrows and handwriting as freehand strokes, the way a hand on a tablet
does: wobbly lines that overshoot their corners, boxes in one stroke or in four, arrow heads drawn as a separate V,
words in the Hershey script font with each letter's strokes jittered and slanted. Prints the board_edit ops (or sends
them with boardctl when --state is given), and writes the scenario's truth - what the sketch means - beside it for the
judge.

    python -I plugins/board/eval/sketch.py checkout --state <dir> --board design/checkout.iedboard --truth <file>
"""

import argparse
import json
import math
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_font(path):
	glyphs = []
	for line in open(path, encoding="ascii"):
		line = line.rstrip("\r\n")
		if not line.strip():
			continue
		count = int(line[5:8])
		data = line[8:8 + count * 2]
		left = ord(data[0]) - ord("R")
		right = ord(data[1]) - ord("R")
		strokes = []
		current = []
		for i in range(2, len(data), 2):
			pair = data[i:i + 2]
			if pair == " R":
				if len(current) > 1:
					strokes.append(current)
				current = []
				continue
			current.append((ord(pair[0]) - ord("R"), ord(pair[1]) - ord("R")))
		if len(current) > 1:
			strokes.append(current)
		glyphs.append((left, right, strokes))
	return glyphs


class Hand:
	def __init__(self, seed, jitter=1.6, cursive=False):
		self.rng = random.Random(seed)
		self.jitter = jitter
		# Print letters by default, as people write on a diagram; --cursive for joined-up writing.
		self.cursive = cursive
		self.font = load_font(os.path.join(HERE, "fonts", "scripts.jhf" if cursive else "futural.jhf"))
		self.ops = []

	def wobble(self, scale=1.0):
		return self.rng.uniform(-1.0, 1.0) * self.jitter * scale

	def stroke(self, points, width=2.5, color=None):
		flat = []
		for x, y in points:
			flat += [round(x, 1), round(y, 1)]
		op = {"op": "add", "type": "stroke", "pts": flat, "width": width}
		if color:
			op["color"] = color
		self.ops.append(op)

	def path(self, corners, step=6.0, scale=1.0):
		"""A wobbly polyline through `corners`, sampled every `step` units."""
		points = []
		drift = 0.0
		for (ax, ay), (bx, by) in zip(corners, corners[1:]):
			length = math.hypot(bx - ax, by - ay)
			n = max(1, int(length / step))
			for k in range(n):
				t = k / n
				drift = drift * 0.8 + self.wobble(scale) * 0.6
				nx, ny = -(by - ay) / max(length, 1e-9), (bx - ax) / max(length, 1e-9)
				points.append((ax + (bx - ax) * t + nx * drift, ay + (by - ay) * t + ny * drift))
		points.append(corners[-1])
		return points

	def box(self, x, y, w, h, strokes=1, color=None):
		o = lambda: self.wobble(3.0)
		tl, tr, br, bl = (x + o(), y + o()), (x + w + o(), y + o()), (x + w + o(), y + h + o()), (x + o(), y + h + o())
		if strokes == 1:
			overshoot = (tl[0] + 6 + o(), tl[1] + o() * 0.5)
			self.stroke(self.path([tl, tr, br, bl, (tl[0] + o(), tl[1] - 3 + o()), overshoot]), color=color)
		else:
			for a, b in ((tl, tr), (tr, br), (br, bl), (bl, tl)):
				self.stroke(self.path([(a[0] + o() * 0.7, a[1] + o() * 0.7), (b[0] + o() * 0.7, b[1] + o() * 0.7)]), color=color)

	def ellipse(self, cx, cy, rx, ry, color=None):
		start = self.rng.uniform(0, math.tau)
		points = []
		for i in range(48):
			a = start + i / 46 * math.tau
			r = 1 + self.wobble(0.012)
			points.append((cx + math.cos(a) * rx * r, cy + math.sin(a) * ry * r))
		self.stroke(points, color=color)

	def diamond(self, cx, cy, w, h, color=None):
		o = lambda: self.wobble(2.0)
		pts = [(cx + o(), cy - h / 2 + o()), (cx + w / 2 + o(), cy + o()), (cx + o(), cy + h / 2 + o()), (cx - w / 2 + o(), cy + o())]
		self.stroke(self.path(pts + [(pts[0][0] + 4, pts[0][1] + 2)]), color=color)

	def arrow(self, ax, ay, bx, by, head=True, bend=0.0, style="separate", color=None):
		mx, my = (ax + bx) / 2, (ay + by) / 2
		length = math.hypot(bx - ax, by - ay)
		nx, ny = -(by - ay) / length, (bx - ax) / length
		corners = [(ax, ay), (mx + nx * bend, my + ny * bend), (bx, by)] if bend else [(ax, ay), (bx, by)]
		shaft = self.path(corners, step=7.0, scale=0.8)
		ux, uy = (bx - ax) / length, (by - ay) / length
		size = min(18.0, length * 0.3)
		left = (bx - ux * size - uy * size * 0.6 + self.wobble(), by - uy * size + ux * size * 0.6 + self.wobble())
		right = (bx - ux * size + uy * size * 0.6 + self.wobble(), by - uy * size - ux * size * 0.6 + self.wobble())
		if head and style == "hook":
			self.stroke(shaft + [left, (bx, by), right], color=color)
			return
		self.stroke(shaft, color=color)
		if head:
			self.stroke(self.path([left, (bx + self.wobble(0.5), by + self.wobble(0.5)), right], step=4.0, scale=0.5), color=color)

	def write(self, text, x, y, size=22.0, color=None):
		"""Handwriting: each letter's strokes from the script font, jittered, slanted and on a wandering baseline."""
		scale = size / (32.0 if self.cursive else 30.0)
		pen = x
		base = y + size * 0.7
		for ch in text:
			code = ord(ch) - 32
			if code < 0 or code >= len(self.font):
				continue
			left, right, strokes = self.font[code]
			lift = self.wobble(0.6)
			for stroke in strokes:
				points = []
				for gx, gy in stroke:
					px = pen + (gx - left) * scale
					py = base + (gy - 9) * scale + lift
					points.append((px - (py - base) * 0.12 + self.wobble(0.25), py + self.wobble(0.25)))
				self.stroke(points, width=2.0, color=color)
			pen += (right - left) * scale + self.wobble(0.4)
		return pen - x


def text_width(hand, text, size):
	scale = size / (32.0 if hand.cursive else 30.0)
	total = 0.0
	for ch in text:
		code = ord(ch) - 32
		if 0 <= code < len(hand.font):
			total += (hand.font[code][1] - hand.font[code][0]) * scale
	return total


def labeled_box(hand, x, y, w, h, label, strokes=1, size=22.0):
	hand.box(x, y, w, h, strokes)
	tw = text_width(hand, label, size)
	hand.write(label, x + (w - tw) / 2, y + h / 2 - size * 0.55, size)


def checkout(hand):
	"""A small web shop: a client, a gateway, three services, a database, a queue; handwritten labels and a note."""
	truth = {"nodes": {"web": "Web App", "gateway": "API Gateway", "orders": "Orders", "payments": "Payments", "db": "Orders DB", "queue": "Event Queue", "email": "Email"}, "edges": [["web", "gateway"], ["gateway", "orders"], ["gateway", "payments"], ["orders", "db"], ["orders", "queue"], ["queue", "email"]], "notes": ["retry payments 3x"], "shapes": {"db": "ellipse", "queue": "rect"}}
	labeled_box(hand, 60, 40, 170, 70, "Web App", strokes=1)
	labeled_box(hand, 360, 40, 200, 70, "API Gateway", strokes=4)
	labeled_box(hand, 250, 230, 160, 70, "Orders", strokes=1)
	labeled_box(hand, 520, 230, 170, 70, "Payments", strokes=1)
	hand.ellipse(170, 430, 100, 45)
	hand.write("Orders DB", 112, 418, 20)
	labeled_box(hand, 400, 400, 190, 70, "Event Queue", strokes=4)
	labeled_box(hand, 700, 400, 140, 70, "Email", strokes=1)
	hand.arrow(236, 76, 350, 76)
	hand.arrow(420, 116, 345, 222, style="hook")
	hand.arrow(510, 116, 590, 222)
	hand.arrow(300, 306, 210, 380)
	hand.arrow(360, 306, 470, 392)
	hand.arrow(596, 436, 692, 436)
	hand.write("retry payments 3x", 720, 250, 18)
	return truth


def engine(hand):
	"""A game engine's modules, a frame-like box drawn around the runtime ones, a decision diamond."""
	truth = {"nodes": {"game": "Game", "ecs": "ECS", "renderer": "Renderer", "assets": "Assets", "input": "Input", "window": "Window", "loaded": "Loaded?"}, "edges": [["game", "ecs"], ["game", "input"], ["ecs", "renderer"], ["renderer", "window"], ["input", "window"], ["renderer", "assets"], ["assets", "loaded"]], "groups": {"Runtime": ["ecs", "renderer", "input", "window"]}, "shapes": {"loaded": "diamond"}}
	labeled_box(hand, 330, 20, 140, 60, "Game", strokes=1)
	hand.box(60, 140, 700, 300, strokes=1)
	hand.write("Runtime", 80, 150, 18)
	labeled_box(hand, 120, 210, 140, 60, "ECS", strokes=4)
	labeled_box(hand, 330, 210, 170, 60, "Renderer", strokes=1)
	labeled_box(hand, 580, 210, 140, 60, "Input", strokes=1)
	labeled_box(hand, 450, 350, 150, 60, "Window", strokes=4)
	labeled_box(hand, 300, 520, 150, 60, "Assets", strokes=1)
	hand.diamond(640, 550, 170, 110)
	hand.write("Loaded?", 594, 538, 20)
	hand.arrow(370, 86, 210, 204)
	hand.arrow(440, 86, 630, 204, bend=30)
	hand.arrow(266, 240, 322, 240)
	hand.arrow(440, 276, 500, 344)
	hand.arrow(640, 276, 560, 344)
	hand.arrow(400, 276, 380, 512, bend=-25)
	hand.arrow(456, 550, 552, 550, style="hook")
	return truth


SCENARIOS = {"checkout": checkout, "engine": engine}


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("scenario", choices=sorted(SCENARIOS))
	parser.add_argument("--seed", type=int, default=1)
	parser.add_argument("--cursive", action="store_true")
	parser.add_argument("--state")
	parser.add_argument("--board")
	parser.add_argument("--truth")
	args = parser.parse_args()
	hand = Hand(args.seed, cursive=args.cursive)
	truth = SCENARIOS[args.scenario](hand)
	if args.truth:
		with open(args.truth, "w", encoding="utf-8") as f:
			json.dump(truth, f, indent=1)
	if not args.state:
		print(json.dumps(hand.ops))
		return 0
	ops_file = os.path.join(args.state, "sketch-ops.json")
	with open(ops_file, "w", encoding="utf-8") as f:
		json.dump({"board": args.board, "ops": [{"op": "update_layer", "id": "L1", "name": "Sketch"}] + hand.ops, "label": "sketch"}, f)
	return subprocess.call([sys.executable, "-I", os.path.join(HERE, "boardctl.py"), "call", "edit", "--args-file", ops_file, "--state", args.state, "--as-owner"])


if __name__ == "__main__":
	sys.exit(main())
