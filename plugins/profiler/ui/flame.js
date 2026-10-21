// flame.js - the flame graph (F-PRF-22) as an icicle: the scope's zones merged by call path, root at the top, each
// node as wide as its share of its parent, children left to right by cost; from call stack samples for imported
// profiles. A click zooms into a node (its ancestors stay as the path above), a click on the path climbs back.

import { h, icon, replace } from "/ui-kit/dom.js";
import { readTheme } from "./timelinedraw.js";
import { duration, count } from "./format.js";
import { scopeLabel } from "./panels.js";

const ROW = 18;

function value(node, unit) {
	return unit === "ns" ? duration(node.total) : count(node.total) + " " + unit;
}

export class FlamePanel {
	constructor({ request, onPick }) {
		this.request = request;
		this.onPick = onPick;
		this.path = [];
		this.summary = h("span.muted");
		this.trail = h("span.prof-trail");
		this.canvas = h("canvas.prof-flame-canvas", { "aria-label": "Flame graph" });
		this.tip = h("div.prof-tip", { hidden: true });
		this.el = h("div.prof-panel", null,
			h("div.panel-tools", null, h("span.muted", null, "Flame Graph"), this.trail, h("span.grow"), this.summary),
			h("div.prof-flame", null, this.canvas, this.tip));
		this.canvas.addEventListener("click", (e) => this.click(e.offsetX, e.offsetY));
		this.canvas.addEventListener("pointermove", (e) => this.hover(e.offsetX, e.offsetY));
		this.canvas.addEventListener("pointerleave", () => { this.tip.hidden = true; });
		this.resizer = new ResizeObserver(() => this.paint());
		this.resizer.observe(this.canvas);
	}

	dispose() {
		this.resizer.disconnect();
	}

	update(captureId, scope) {
		this.captureId = captureId;
		this.scope = scope;
		this.load();
	}

	async load() {
		if (!this.captureId) return;
		const asked = { captureId: this.captureId };
		if (this.scope && this.scope.from !== undefined) Object.assign(asked, { from: Math.round(this.scope.from), to: Math.round(this.scope.to) });
		const answer = await this.request("prof.flame", asked);
		if (!answer) return;
		this.answer = answer;
		this.path = [answer.root];
		this.summary.textContent = (answer.source === "samples" ? "from samples \u00b7 " : "from zones \u00b7 ") + scopeLabel(this.scope).replace("Range: ", "");
		this.renderTrail();
		this.paint();
	}

	get focus() {
		return this.path[this.path.length - 1];
	}

	renderTrail() {
		replace(this.trail, this.path.map((node, i) => [i ? icon("chevron-right", "sm") : null,
			h("span.prof-crumb", { on: { click: () => { this.path = this.path.slice(0, i + 1); this.renderTrail(); this.paint(); } } }, node.name)]));
	}

	// Each drawn node with its box: {node, x, y, w}.
	boxes(width) {
		const out = [];
		const walk = (node, x, w, depth) => {
			out.push({ node, x, y: depth * ROW, w });
			const total = Math.max(1, node.total);
			let cx = x;
			for (const child of node.children) {
				const cw = child.total / total * w;
				if (cw >= 0.5) walk(child, cx, cw, depth + 1);
				cx += cw;
			}
		};
		if (this.focus) walk(this.focus, 0, width, 0);
		return out;
	}

	paint() {
		if (!this.answer) return;
		const width = Math.max(1, this.canvas.clientWidth);
		const height = Math.max(1, this.canvas.clientHeight);
		const ratio = window.devicePixelRatio || 1;
		this.canvas.width = width * ratio;
		this.canvas.height = height * ratio;
		const ctx = this.canvas.getContext("2d");
		ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
		const theme = readTheme();
		ctx.fillStyle = theme.bg2;
		ctx.fillRect(0, 0, width, height);
		ctx.font = theme.fontXs + "px " + theme.fontUi;
		ctx.textBaseline = "middle";
		this.drawn = this.boxes(width);
		for (const { node, x, y, w } of this.drawn) {
			if (y > height) continue;
			ctx.fillStyle = theme.zones[(y / ROW) % 4];
			ctx.fillRect(x, y, Math.max(1, w - 1), ROW - 1);
			if (w < 30) continue;
			ctx.fillStyle = theme.text0;
			const text = node.name + "  " + value(node, this.answer.unit);
			let shown = text;
			while (shown.length > 1 && ctx.measureText(shown).width > w - 8) shown = shown.slice(0, -2);
			if (shown !== text) shown = shown.slice(0, -1) + "\u2026";
			ctx.fillText(shown, x + 4, y + ROW / 2);
		}
	}

	nodeAt(x, y) {
		return (this.drawn || []).find((box) => x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + ROW) || null;
	}

	click(x, y) {
		const box = this.nodeAt(x, y);
		if (!box) return;
		if (box.node === this.focus && this.path.length > 1) this.path.pop();
		else if (box.node !== this.focus) {
			const chain = [];
			const find = (node) => {
				chain.push(node);
				if (node === box.node) return true;
				for (const child of node.children) if (find(child)) return true;
				chain.pop();
				return false;
			};
			find(this.focus);
			this.path = this.path.concat(chain.slice(1));
			if (this.onPick) this.onPick(box.node.name);
		}
		this.renderTrail();
		this.paint();
	}

	hover(x, y) {
		const box = this.nodeAt(x, y);
		if (!box) {
			this.tip.hidden = true;
			return;
		}
		const share = this.focus.total ? (box.node.total / this.focus.total * 100).toFixed(1) : "0";
		this.tip.textContent = box.node.name + " \u00b7 " + value(box.node, this.answer.unit) + " (" + share + "%) \u00b7 self " +
			(this.answer.unit === "ns" ? duration(box.node.self) : count(box.node.self)) + " \u00b7 " + count(box.node.count) + "x";
		this.tip.hidden = false;
		this.tip.style.left = Math.min(x + 12, this.canvas.clientWidth - this.tip.offsetWidth - 4) + "px";
		this.tip.style.top = (y + 14) + "px";
	}
}
