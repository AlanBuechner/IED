// editpane.js - Edit and Replay (F-GPU-42, docs/22 "Edit and Replay"): a stage's shader edited and the frame replayed
// with it. The edit is a workspace file - opened in IED's editor, where it is an ordinary document change, saved or
// not, and compiled as the editor holds it - or text edited here: the captured SPIR-V listing, a module's own source,
// pasted HLSL. Apply (Ctrl+Enter) compiles; problems list with their lines, and a click goes there.

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT } from "./format.js";

const LANGUAGES = { hlsl: "HLSL", spirv: "SPIR-V listing" };
const STAGES = { vs: "Vertex", ps: "Pixel", fs: "Fragment", cs: "Compute", gs: "Geometry", hs: "Hull", ds: "Domain" };

export class EditPane {
	// onApply(request) resolves with gpu.editShader's answer; onRevert(key) puts the captured shader back.
	constructor({ ied, onClose, onApply, onRevert }) {
		this.ied = ied;
		this.onApply = onApply;
		this.onRevert = onRevert;
		this.title = h("b");
		this.language = h("select", { title: "What the edit is written in", on: { change: () => this.modeChanged() } });
		this.path = h("input.input-field.gpu-edit-path", { placeholder: "Workspace file, or empty to edit here", title: "A workspace file holding the edited shader: compiled as IED's editor holds it, unsaved changes included", on: { input: () => this.modeChanged() } });
		this.openButton = h("button.btn.flat", { title: "Open the file in the editor", on: { click: () => this.openFile() } }, icon("go-to-file"), "Open");
		this.entry = h("input.input-field.gpu-edit-entry", { placeholder: "Entry", title: "The HLSL entry point" });
		this.profile = h("input.input-field.gpu-edit-profile", { placeholder: "Profile", title: "The HLSL target profile, e.g. ps_5_1" });
		this.applyButton = h("button.btn", { title: "Compile and replay the frame with the edit (Ctrl+Enter)", on: { click: () => this.apply() } }, icon("play"), "Apply");
		this.revertButton = h("button.btn.flat", { title: "Put the captured shader back", hidden: true, on: { click: () => this.revert() } }, icon("discard"), "Revert");
		this.status = h("span.gpu-edit-status");
		this.text = h("textarea.gpu-edit-text", { spellcheck: false, wrap: "off", "aria-label": "The edited shader", on: { keydown: (e) => this.key(e) } });
		this.fileNote = h("div.gpu-state-msg.gpu-edit-note", { hidden: true });
		this.problems = h("div.gpu-edit-problems");
		this.el = h("div.gpu-edit", null,
			h("div.gpu-rv-tools", null, icon("edit"), this.title, h("span.grow"), h("button.ib", { title: "Back to the resource", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			h("div.gpu-rv-tools.gpu-edit-form", null, this.language, this.path, this.openButton, this.entry, this.profile),
			this.text, this.fileNote, this.problems,
			h("div.gpu-rv-tools", null, this.applyButton, this.revertButton, this.status));
	}

	message(text, error) {
		this.title.textContent = "Edit and Replay";
		this.state = null;
		replace(this.problems, h("div.gpu-state-msg" + (error ? ".error" : ""), null, text));
		this.text.hidden = true;
		this.fileNote.hidden = true;
	}

	// `source` is gpu.shaderSource's answer for the stage at `eid`.
	show(source, eid) {
		this.state = { key: source.key, eid, stage: source.stage, edited: source.edited };
		this.title.textContent = "Edit and Replay " + DOT + " " + (STAGES[source.stage] || source.stage) + " shader " + DOT + " EID " + eid + " " + DOT + " " + source.pipeline + " " + DOT + " " + source.kind;
		replace(this.language, source.languages.map((id) => h("option", { value: id }, LANGUAGES[id] || id)));
		const link = source.link || {};
		this.language.value = link.language && source.languages.includes(link.language) ? link.language : source.language;
		this.path.value = link.path || "";
		this.entry.value = link.entry || source.entry || "main";
		this.profile.value = link.profile || source.profile || "";
		const embedded = source.embedded && source.embedded.language === this.language.value ? source.embedded.source : "";
		this.text.value = source.source || embedded || "";
		this.text.placeholder = "The edited " + (LANGUAGES[this.language.value] || "shader") + ". A compiled shader keeps no source: paste it, or name the workspace file it comes from above.";
		this.revertButton.hidden = !source.edited;
		this.status.textContent = source.edited ? "Edited in this replay." : "";
		replace(this.problems);
		this.modeChanged();
	}

	// A file edits in the editor; without one the text edits here. HLSL needs its entry and profile.
	modeChanged() {
		const file = this.path.value.trim();
		this.text.hidden = !!file;
		this.fileNote.hidden = !file;
		this.fileNote.textContent = file ? "Edit " + file + " in the editor: Apply compiles it as the editor holds it, saved or not." : "";
		this.openButton.disabled = !file;
		const hlsl = this.language.value === "hlsl";
		this.entry.hidden = !hlsl;
		this.profile.hidden = !hlsl;
	}

	openFile() {
		const path = this.path.value.trim();
		if (path) this.ied.commands.run("ied.editor.open", { path });
	}

	key(e) {
		if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
			e.preventDefault();
			this.apply();
		}
		else if (e.key === "Tab" && !e.shiftKey && !e.ctrlKey) {
			e.preventDefault();
			this.text.setRangeText("\t", this.text.selectionStart, this.text.selectionEnd, "end");
		}
	}

	async apply() {
		if (!this.state) return;
		const path = this.path.value.trim();
		const request = { eid: this.state.eid, stage: this.state.stage, language: this.language.value, entry: this.entry.value.trim(), profile: this.profile.value.trim() };
		if (path) request.path = path;
		else request.text = this.text.value;
		this.applyButton.disabled = true;
		this.status.textContent = "Compiling and replaying...";
		replace(this.problems);
		try {
			const result = await this.onApply(request);
			this.showProblems(result.diagnostics || [], path);
			if (result.ok) {
				this.state.key = result.key;
				this.revertButton.hidden = false;
				this.status.textContent = "Applied to " + (result.pipelines || []).join(", ") + ((result.diagnostics || []).length ? " with warnings" : "") + ".";
			}
			else this.status.textContent = "Not applied: the replay keeps the shader it had.";
		}
		catch (e) {
			this.status.textContent = e.message || String(e);
		}
		finally {
			this.applyButton.disabled = false;
		}
	}

	async revert() {
		if (!this.state) return;
		await this.onRevert(this.state.key);
		this.revertButton.hidden = true;
		this.status.textContent = "The captured shader is back.";
		replace(this.problems);
	}

	showProblems(diagnostics, path) {
		replace(this.problems, diagnostics.map((d) => h("div.gpu-r.gpu-edit-problem", { title: d.text, on: { click: () => this.goTo(d, path) } },
			h("span", null, icon(d.severity === "error" ? "error" : d.severity === "warning" ? "warning" : "info", d.severity === "error" ? "c-error" : d.severity === "warning" ? "c-warn" : "c-info")),
			h("span.n.d", null, d.line ? d.line + (d.column ? ":" + d.column : "") : ""), h("span", null, d.text))));
	}

	// A problem's place: in the editor for a file, else in the text here.
	goTo(d, path) {
		if (!d.line) return;
		if (path) {
			this.ied.commands.run("ied.editor.open", { path, line: d.line, col: d.column || 1 });
			return;
		}
		const lines = this.text.value.split("\n");
		const start = lines.slice(0, d.line - 1).reduce((n, l) => n + l.length + 1, 0) + Math.max(0, (d.column || 1) - 1);
		const end = lines.slice(0, d.line).reduce((n, l) => n + l.length + 1, 0) - 1;
		this.text.focus();
		this.text.setSelectionRange(start, Math.max(start, end));
		this.text.scrollTop = Math.max(0, (d.line - 5) * (parseFloat(getComputedStyle(this.text).lineHeight) || 16));
	}
}
