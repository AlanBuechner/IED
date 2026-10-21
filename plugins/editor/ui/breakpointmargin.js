// breakpointmargin.js - an editor's glyph margin while it shows a workspace file (VS): a dot per breakpoint (hollow while
// the current session has not bound it, gray when disabled, on the line the session bound it to), a click to set or
// clear one, and the current statement - the arrow and the line's background - when the session stops in the file
// (another frame of the stack shown with a lighter arrow).

import { framePath } from "./debug.js";

// The decorations for `path`: its breakpoints as the session bound them, and the frame shown.
export function marginDecorations(path, breakpoints, bound, frame, isTopFrame) {
	const decorations = [];
	breakpoints.forEach((breakpoint, i) => {
		const binding = bound.find((b) => b.line === breakpoint.line) || null;
		const line = binding && binding.verified ? binding.boundLine : breakpoint.line;
		const kind = !breakpoint.enabled ? "bp-disabled" : binding && !binding.verified ? "bp-unverified" : "bp-set";
		const hover = binding && binding.message ? binding.message : !breakpoint.enabled ? "Disabled breakpoint" : "Breakpoint";
		decorations.push({ line, glyph: "bp-glyph " + kind, hover, key: i });
	});
	if (frame && framePath(frame) === path) decorations.push({ line: frame.line, glyph: isTopFrame ? "dbg-arrow" : "dbg-arrow dbg-caller", whole: isTopFrame ? "dbg-current-line" : "dbg-caller-line" });
	return decorations;
}

export class BreakpointMargin {
	constructor(editor, monaco, breakpoints, debug) {
		this.editor = editor;
		this.monaco = monaco;
		this.breakpoints = breakpoints;
		this.debug = debug;
		this.marks = editor.editor.createDecorationsCollection();
		this.workspaceFile = !/^[a-z]+:/i.test(editor.path);
		editor.editor.updateOptions({ glyphMargin: this.workspaceFile });
		this.clicks = editor.editor.onMouseDown((event) => this.clicked(event));
		this.stopBreakpoints = breakpoints.on(() => this.redraw());
		this.stopDebug = debug.on(() => this.redraw());
		this.redraw();
	}

	clicked(event) {
		if (!this.workspaceFile || event.target.type !== this.monaco.editor.MouseTargetType.GUTTER_GLYPH_MARGIN || !event.target.position) return;
		this.breakpoints.toggle(this.editor.path, event.target.position.lineNumber - 1);
	}

	redraw() {
		const session = this.debug.session;
		const bound = session ? this.breakpoints.boundIn(session.id, this.editor.path) : [];
		const frame = this.debug.stopped() ? this.debug.frame : null;
		const decorations = marginDecorations(this.editor.path, this.workspaceFile ? this.breakpoints.of(this.editor.path) : [], bound, frame, frame === this.debug.frames[0]);
		this.marks.set(decorations.map((d) => ({
			range: { startLineNumber: d.line + 1, startColumn: 1, endLineNumber: d.line + 1, endColumn: 1 },
			options: { glyphMarginClassName: d.glyph, glyphMarginHoverMessage: d.hover ? { value: d.hover } : undefined, className: d.whole, isWholeLine: !!d.whole, stickiness: 1 },
		})));
	}

	dispose() {
		this.clicks.dispose();
		this.stopBreakpoints();
		this.stopDebug();
		this.marks.clear();
	}
}
