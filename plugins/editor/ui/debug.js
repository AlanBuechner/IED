// debug.js - the workspace's debugging (docs/25): the sessions from dbg:<wsId> and the current one's stops from its own
// topic, the breakpoints from bp:<wsId>, the commands (F5 starts or continues, F10 / F11 / Shift+F11 step, Shift+F5
// stops, F9 toggles a breakpoint) and the toolbar's run controls. Everything is the server's, so every window shows the
// same session at the same stop, and a control from a stale stop is refused (the session's revision).

import { h, icon, replace } from "/ui-kit/dom.js";

// A frame's file as a tab knows it: a workspace path, or a file: URI outside the workspace; null for neither.
export function framePath(frame) {
	const source = frame && frame.source;
	if (!source) return null;
	return source.path || source.uri || null;
}

// The session the toolbar and the views follow: the newest live one, else the newest.
export function currentSession(sessions) {
	const live = sessions.filter((s) => s.state !== "ended" && s.state !== "failed");
	return live.length ? live[live.length - 1] : sessions.length ? sessions[sessions.length - 1] : null;
}

export function isLive(session) {
	return !!session && session.state !== "ended" && session.state !== "failed";
}

export class DebugState {
	constructor(ied) {
		this.ied = ied;
		this.wsId = null;
		this.sessions = [];
		this.session = null;      // the current session's summary
		this.frames = [];
		this.threads = [];
		this.frame = null;        // the frame the views show (the top one at each stop)
		this.listeners = new Set();
		this.stopWorkspace = null;
		this.stopSession = null;
		this.followed = null;     // the session id whose topic is followed
		this.thread = null;       // the thread a click in Threads chose; null: the stopped one
		this.onStop = null;       // fn(frame) when a stop arrives: the shell shows its place
	}

	follow(wsId) {
		if (wsId === this.wsId) return;
		if (this.stopWorkspace) this.stopWorkspace();
		this.wsId = wsId;
		this.sessions = [];
		this.followSession(null);
		this.stopWorkspace = wsId ? this.ied.client.subscribe("dbg:" + wsId, {
			snap: (data) => this.sessionsAre(data.sessions || []),
			evt: (kind, data) => {
				if (kind === "dbg.session") this.sessionsAre(this.sessions.filter((s) => s.id !== data.id).concat(data));
			},
		}) : null;
	}

	sessionsAre(sessions) {
		this.sessions = sessions;
		const current = currentSession(sessions);
		if (current && current.id === (this.session && this.session.id)) this.session = current;
		else this.followSession(current);
		this.changed();
	}

	followSession(session) {
		if (this.stopSession) this.stopSession();
		this.stopSession = null;
		this.session = session;
		this.frames = [];
		this.threads = [];
		this.frame = null;
		this.followed = session ? session.id : null;
		if (!session) return;
		const id = session.id;
		this.stopSession = this.ied.client.subscribe("dbg:" + this.wsId + ":" + id, {
			snap: (data) => {
				if (this.followed !== id) return;
				this.session = data.session;
				this.stoppedAt(data.frames || [], data.threads || [], false);
			},
			evt: (kind, data) => {
				if (this.followed !== id) return;
				if (kind === "dbg.state") {
					this.session = data;
					if (data.state !== "stopped") this.stoppedAt([], this.threads, false);
					else this.changed();
				}
				else if (kind === "dbg.stopped") {
					this.session = data.session;
					this.stoppedAt(data.frames || [], data.threads || [], true);
				}
			},
		});
	}

	stoppedAt(frames, threads, shown) {
		this.thread = null;
		this.frames = frames;
		this.threads = threads;
		this.frame = frames[0] || null;
		this.changed();
		if (shown && this.frame && this.onStop) this.onStop(this.frame);
	}

	on(fn) {
		this.listeners.add(fn);
		fn(this);
		return () => this.listeners.delete(fn);
	}

	changed() {
		const state = isLive(this.session) ? this.session.state : "";
		this.ied.context.set("debugState", state || undefined);
		for (const fn of [...this.listeners]) fn(this);
	}

	live() {
		return isLive(this.session);
	}

	stopped() {
		return !!this.session && this.session.state === "stopped";
	}

	selectFrame(frame) {
		this.frame = frame;
		this.changed();
	}

	// Threads: another thread's frames, the top one shown.
	async selectThread(threadId) {
		if (!this.stopped()) return;
		try {
			const trace = await this.ied.client.request("dbg.stackTrace", { sessionId: this.session.id, threadId });
			this.thread = threadId;
			this.frames = trace.stackFrames || [];
			this.frame = this.frames[0] || null;
			this.changed();
			if (this.frame && this.onStop) this.onStop(this.frame);
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	// The thread the frames are of: the one a click in Threads chose, else the stopped one.
	threadShown() {
		return this.thread || (this.session && this.session.stop ? this.session.stop.threadId : null);
	}

	// An expression's value in the shown frame (watch, hover): {result, type, variablesReference} or {error}.
	async evaluate(expression, context) {
		if (!this.stopped()) return { error: "not stopped" };
		try {
			return await this.ied.client.request("dbg.evaluate", { sessionId: this.session.id, frameId: this.frame ? this.frame.id : undefined, expression, context });
		}
		catch (e) {
			return { error: e.message };
		}
	}

	// F5: continue a stopped session, start one when none runs.
	async start() {
		if (this.stopped()) return this.control("continue");
		if (this.live()) return null;
		try {
			const started = await this.ied.client.request("dbg.start", { wsId: this.wsId });
			await this.ied.commands.run("ied.output.show", { sourceId: "debug" });
			return started;
		}
		catch (e) {
			this.ied.toast(e.message, { level: e.code === "not_found" ? "info" : "error" });
			return null;
		}
	}

	// continue, next, stepIn, stepOut, pause: from the stop this window shows, so a stale one is refused.
	async control(command) {
		const session = this.session;
		if (!session) return null;
		try {
			return await this.ied.client.request("dbg." + command, { sessionId: session.id, threadId: session.stop ? session.stop.threadId : undefined, revision: session.revision });
		}
		catch (e) {
			this.ied.toast(e.message, { level: e.code === "conflict" ? "info" : "error" });
			return null;
		}
	}

	async stop() {
		if (!this.live()) return;
		await this.ied.client.request("dbg.stop", { sessionId: this.session.id }).catch((e) => this.ied.toast(e.message, { level: "error" }));
	}

	async restart() {
		if (!this.session) return;
		await this.ied.client.request("dbg.restart", { sessionId: this.session.id }).catch((e) => this.ied.toast(e.message, { level: "error" }));
	}
}

// The workspace's breakpoints (bp:<wsId>): each file's, and where the current session bound them.
export class BreakpointState {
	constructor(ied) {
		this.ied = ied;
		this.wsId = null;
		this.files = new Map();   // path -> breakpoints
		this.bound = new Map();   // "<sessionId> <path>" -> bound breakpoints
		this.watches = [];        // the Watch window's expressions
		this.listeners = new Set();
		this.stop = null;
	}

	follow(wsId) {
		if (wsId === this.wsId) return;
		if (this.stop) this.stop();
		this.wsId = wsId;
		this.files = new Map();
		this.bound = new Map();
		this.stop = wsId ? this.ied.client.subscribe("bp:" + wsId, {
			snap: (data) => {
				this.files = new Map((data.files || []).map((f) => [f.path, f.breakpoints]));
				this.bound = new Map((data.bound || []).map((b) => [b.sessionId + " " + b.path, b.breakpoints]));
				this.watches = data.watches || [];
				this.changed();
			},
			evt: (kind, data) => {
				if (kind === "bp.changed") this.files.set(data.path, data.breakpoints);
				else if (kind === "bp.bound") this.bound.set(data.sessionId + " " + data.path, data.breakpoints);
				else if (kind === "bp.unbound") {
					for (const key of [...this.bound.keys()]) if (key.startsWith(data.sessionId + " ")) this.bound.delete(key);
				}
				else if (kind === "watch.changed") this.watches = data.expressions || [];
				this.changed();
			},
		}) : null;
	}

	on(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	}

	changed() {
		for (const fn of [...this.listeners]) fn(this);
	}

	of(path) {
		return this.files.get(path) || [];
	}

	boundIn(sessionId, path) {
		return this.bound.get(sessionId + " " + path) || [];
	}

	// Every file's breakpoints: [{path, breakpoint}], files in path order.
	all() {
		return [...this.files.entries()].sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)).flatMap(([path, list]) => list.map((breakpoint) => ({ path, breakpoint })));
	}

	async set(path, breakpoints) {
		try {
			await this.ied.client.request("bp.set", { wsId: this.wsId, path, breakpoints });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	async setWatches(expressions) {
		try {
			await this.ied.client.request("watch.set", { wsId: this.wsId, expressions });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}

	// F9 and a click in the margin: a breakpoint on `line` (0-based) goes, else one comes.
	async toggle(path, line) {
		const list = this.of(path);
		const next = list.some((b) => b.line === line) ? list.filter((b) => b.line !== line) : list.concat({ line, enabled: true }).sort((a, b) => a.line - b.line);
		try {
			await this.ied.client.request("bp.set", { wsId: this.wsId, path, breakpoints: next });
		}
		catch (e) {
			this.ied.toast(e.message, { level: "error" });
		}
	}
}

// The toolbar's debugging part: Start Debugging with the startup project's configuration (VS's "Local Windows Debugger")
// when no session lives; while one does, Continue or Break All, Stop, Restart, and the steps.
export function DebugToolbar(ctx, state, tasks) {
	state.follow(ctx.wsId);
	const button = (label, glyph, run, enabled) => h("button.ib" + (enabled ? "" : ".dim"), { title: label, "aria-label": label, disabled: !enabled, on: { click: run } }, icon(glyph));
	const render = () => {
		if (!state.live()) {
			const debug = tasks && tasks.offer ? tasks.offer.debug : null;
			const name = debug && debug.configurations.length ? debug.default || debug.configurations[0] : null;
			return name ? replace(ctx.el, h("button.ib.tb-start", { title: "Start Debugging (F5)", "aria-label": "Start Debugging", on: { click: () => state.start() } }, icon("debug-start", "c-saved"), h("span.tb-label", null, name))) : replace(ctx.el);
		}
		const stopped = state.stopped();
		replace(ctx.el,
			stopped ? button("Continue (F5)", "debug-continue", () => state.control("continue"), true) : button("Break All", "debug-pause", () => state.control("pause"), state.session.state === "running"),
			button("Stop Debugging (Shift+F5)", "debug-stop", () => state.stop(), true),
			button("Restart (Ctrl+Shift+F5)", "debug-restart", () => state.restart(), true),
			h("span.tb-sep"),
			button("Step Over (F10)", "debug-step-over", () => state.control("next"), stopped),
			button("Step Into (F11)", "debug-step-into", () => state.control("stepIn"), stopped),
			button("Step Out (Shift+F11)", "debug-step-out", () => state.control("stepOut"), stopped));
	};
	const stopDebug = state.on(render);
	const stopTasks = tasks ? tasks.on(render) : () => {};
	return { dispose: () => { stopDebug(); stopTasks(); } };
}

// The expression a hover at `col` (0-based) of `line` names: the identifier there with the member accesses before it
// ("player.position.x", "renderer->width"), or null on anything else.
export function hoverExpression(line, col) {
	const word = /[A-Za-z_][A-Za-z0-9_]*/g;
	let found = null;
	for (let m = word.exec(line); m; m = word.exec(line)) {
		if (m.index <= col && col < m.index + m[0].length) {
			found = { start: m.index, end: m.index + m[0].length };
			break;
		}
	}
	if (!found) return null;
	let start = found.start;
	for (;;) {
		const before = line.slice(0, start);
		const access = /([A-Za-z_][A-Za-z0-9_]*)(\.|->)$/.exec(before);
		if (!access) break;
		start = access.index;
	}
	return { start, end: found.end, text: line.slice(start, found.end) };
}
