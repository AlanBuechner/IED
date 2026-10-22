// history.js - Pixel History (F-GPU-40, docs/22 "Pixel History"): every event up to the selected one that wrote, or
// tried to write, the picked pixel - its value before and after with swatches, the test that kept a draw out - and the
// replayer's summary in words. A row selects its event; the panel stays while events are selected.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT } from "./format.js";

// A texel's values as a CSS color - the pixel's own data, not a theme color: three or more channels as RGB, one as
// gray, each clamped to 0..1.
function swatchColor(values) {
	const v = (values || []).map((x) => (Number.isFinite(x) ? Math.min(1, Math.max(0, x)) : 0));
	if (!v.length) return "transparent";
	const [r, g, b] = v.length >= 3 ? v : [v[0], v[0], v[0]];
	return "color(srgb " + r.toFixed(4) + " " + g.toFixed(4) + " " + b.toFixed(4) + ")";
}

function texel(value) {
	return h("span.gpu-hist-value", { title: value.raw ? "0x" + value.raw : value.text }, h("span.gpu-swatch", { style: { background: swatchColor(value.values) } }), value.text);
}

export class PixelHistoryPanel {
	constructor({ onClose, onSelect, onDebug }) {
		this.onSelect = onSelect;
		this.onDebug = onDebug;
		this.title = h("b");
		this.summary = h("div.gpu-hist-summary");
		this.list = h("div.gpu-hist");
		this.el = h("div.gpu-shader.gpu-history", null,
			h("div.gpu-rv-tools", null, icon("history"), this.title, h("span.grow"),
				h("button.ib", { title: "Back to the resource", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			this.summary, this.list);
	}

	message(text, error) {
		this.title.textContent = "Pixel History";
		replace(this.summary);
		replace(this.list, h("div.gpu-state-msg" + (error ? ".error" : ""), null, text));
	}

	show(history) {
		this.history = history;
		this.title.textContent = "Pixel History " + DOT + " " + history.name + " (" + history.x + ", " + history.y + ")" + (history.mip ? " mip " + history.mip : "") + " " + DOT + " up to EID " + history.eid;
		this.summary.textContent = history.summary;
		this.render();
	}

	// Marks the selected event's row.
	select(eid) {
		this.selected = eid;
		if (this.history) this.render();
	}

	render() {
		const history = this.history;
		const rows = [h("div.gpu-hist-row.edge", null, h("span.eid"), h("span.what", null, "Frame start"), h("span.vals", null, texel(history.start)))];
		for (const e of history.events) {
			const failed = e.result !== "passed" && e.result !== "written";
			rows.push(h("div.gpu-hist-row" + (failed ? ".failed" : "") + (e.eid === this.selected ? ".sel" : ""), { title: e.name, on: { click: () => this.onSelect(e.eid) } },
				h("span.eid", null, String(e.eid)),
				h("span.what", null, h("span.name", null, e.name), h("span.result", null, e.text + (e.samples !== undefined ? " " + DOT + " " + e.samples + " sample" + (e.samples === 1 ? "" : "s") : ""))),
				h("span.vals", null, texel(e.before), icon("arrow-right"), texel(e.after),
					e.kind === "draw" ? h("button.ib", { title: "Debug this draw's fragment shader at the pixel", "aria-label": "Debug", on: { click: (ev) => { ev.stopPropagation(); this.onDebug(e.eid, history.x, history.y); } } }, icon("debug-alt")) : null)));
		}
		rows.push(h("div.gpu-hist-row.edge", null, h("span.eid"), h("span.what", null, "After EID " + history.eid + (history.unattributed ? " (changed by something not listed)" : "")), h("span.vals", null, texel(history.end))));
		replace(this.list, rows);
	}
}
