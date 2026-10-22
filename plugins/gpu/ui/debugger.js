// debugger.js - the shader debugger (F-GPU-41, docs/22 "Shader debugging"): a stage's listing beside the values the
// chosen invocation computed, stepped forward and back through the trace the replay recorded - stepping back is reading
// it backwards. The current step's line is marked; the values pane shows that step's value, then every value made so
// far, newest first. F10 steps, Shift+F10 steps back; a click on a line runs to its next step.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT } from "./format.js";

// A site's name as its listing line gives it: the result ("%color = ..."), else what a store writes ("OpStore %p %v").
function siteName(line, store) {
	const text = (line || "").trim();
	if (store) return text.split(/\s+/)[1] || "?";
	const at = text.indexOf(" = ");
	return at > 0 ? text.slice(0, at) : text.split(/\s+/)[0];
}

export class ShaderDebugPanel {
	constructor({ onClose }) {
		this.title = h("b");
		this.invocationSelect = h("select", { title: "Invocation", hidden: true, on: { change: () => this.pick(+this.invocationSelect.value, 0) } });
		this.counter = h("span.range");
		const button = (name, title, click) => h("button.ib", { title, "aria-label": title, on: { click } }, icon(name));
		this.listing = h("div.gpu-dbg-listing", { tabindex: "0" });
		this.values = h("div.gpu-dbg-values");
		this.el = h("div.gpu-shader.gpu-debugger", { on: { keydown: (e) => this.key(e) } },
			h("div.gpu-rv-tools", null, icon("debug-alt"), this.title, h("span.grow"),
				h("button.ib", { title: "Back to the resource", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			h("div.gpu-rv-tools.gpu-dbg-tools", null, this.invocationSelect,
				button("debug-restart", "To the first step", () => this.go(0)),
				button("debug-step-back", "Step back (Shift+F10)", () => this.go(this.step - 1)),
				button("debug-step-over", "Step (F10)", () => this.go(this.step + 1)),
				button("debug-continue", "To the last step", () => this.go(this.steps().length - 1)),
				this.counter),
			h("div.gpu-dbg-body", null, this.listing, this.values));
	}

	message(text, error) {
		this.trace = null;
		this.title.textContent = "Shader Debugger";
		this.counter.textContent = "";
		this.invocationSelect.hidden = true;
		replace(this.values);
		replace(this.listing, h("div.gpu-state-msg" + (error ? ".error" : ""), null, text));
	}

	// `trace` is the replayer's debugShader answer; `what` says which invocation it is ("pixel (3, 4)").
	show(trace, what) {
		this.trace = trace;
		this.lines = trace.listing.split("\n");
		const stage = { vs: "Vertex", fs: "Fragment", cs: "Compute" }[trace.stage] || trace.stage;
		this.title.textContent = stage + " shader " + DOT + " " + what + " " + DOT + " EID " + trace.eid + " " + DOT + " " + trace.pipeline;
		const count = trace.invocations.length;
		replace(this.invocationSelect, trace.invocations.map((_, i) => h("option", { value: String(i) }, (trace.stage === "fs" ? "Fragment " : "Invocation ") + (i + 1) + " of " + count)));
		this.invocationSelect.hidden = count < 2;
		if (!count) {
			this.counter.textContent = "";
			replace(this.values);
			replace(this.listing, h("div.gpu-state-msg", null, "No invocation ran for this " + (trace.stage === "fs" ? "pixel: the draw covers no fragment there." : trace.stage === "vs" ? "vertex." : "thread.")));
			return;
		}
		this.pick(0, 0);
	}

	steps() { return this.trace ? this.trace.invocations[this.invocation].steps : []; }

	pick(invocation, step) {
		this.invocation = invocation;
		this.invocationSelect.value = String(invocation);
		// The lines this invocation ran, dim the rest.
		this.ran = new Set(this.steps().map((s) => this.trace.sites[s[0]][0]));
		this.lineEls = this.lines.map((text, i) => h("div.l" + (this.ran.has(i) ? ".ran" : ""), { on: { click: (e) => this.runTo(i, e.altKey) } }, text || " "));
		replace(this.listing, this.lineEls);
		this.step = -1;
		this.go(step);
	}

	go(step) {
		const steps = this.steps();
		if (!steps.length) return;
		step = Math.max(0, Math.min(steps.length - 1, step));
		if (this.current !== undefined && this.lineEls[this.current]) this.lineEls[this.current].classList.remove("sel");
		this.step = step;
		const line = this.trace.sites[steps[step][0]][0];
		this.current = line;
		this.counter.textContent = "Step " + (step + 1) + " of " + steps.length + (this.trace.invocations[this.invocation].truncated ? " (the trace is cut)" : "");
		// The values first: they size the listing the line scrolls in.
		this.renderValues();
		if (this.lineEls[line]) {
			this.lineEls[line].classList.add("sel");
			this.listing.scrollTop = this.lineEls[line].offsetTop - this.listing.clientHeight / 2;
		}
	}

	// The next step on line `line` after the current one (the one before with `back`), wrapping around.
	runTo(line, back) {
		const steps = this.steps();
		for (let k = 1; k <= steps.length; k++) {
			const at = back ? (this.step - k + steps.length) % steps.length : (this.step + k) % steps.length;
			if (this.trace.sites[steps[at][0]][0] === line) return this.go(at);
		}
	}

	renderValues() {
		const steps = this.steps();
		const latest = new Map();
		for (let k = 0; k <= this.step; k++) {
			const [siteIndex, value] = steps[k];
			const [line, , , store] = this.trace.sites[siteIndex];
			const name = siteName(this.lines[line], store);
			latest.delete(name);
			latest.set(name, { value, line, step: k });
		}
		const [siteIndex, value] = steps[this.step];
		const [line, , , store] = this.trace.sites[siteIndex];
		const rows = [h("div.gpu-srow.hd", null, h("span.k", null, "This step"), h("span.v", null, "line " + (line + 1))),
			h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, siteName(this.lines[line], store)), h("span.v", null, value)),
			h("div.gpu-srow.hd", null, h("span.k", null, "Values so far"), h("span.v", null, String(latest.size)))];
		for (const [name, seen] of [...latest.entries()].reverse())
			rows.push(h("div.gpu-srow.link" + (seen.step === this.step ? ".sel" : ""), { style: { "--d": 1 }, title: "Step " + (seen.step + 1), on: { click: () => this.go(seen.step) } },
				h("span.k", null, name), h("span.v", null, seen.value)));
		replace(this.values, rows);
	}

	key(e) {
		if (!this.trace || e.key !== "F10") return;
		e.preventDefault();
		this.go(this.step + (e.shiftKey ? -1 : 1));
	}
}
