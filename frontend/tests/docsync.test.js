// docsync.test.js - document editing between this client and the server: the rebase rules on both sides agree (a
// seeded random run), and DocumentSync against a model of the server's document - acknowledgements by event, another
// writer's edits arriving while this client's are in flight or waiting, the lease, saving, and a resync on overlap.

import { test, assertEqual, assertTrue } from "./harness.js";
import { applyChanges, rebase, transformAfter, transformBefore, OverlapError, lineStartsOf } from "/ui-kit/textedits.js";
import { DocumentSync } from "/ui-kit/docsync.js";

function change(line, col, endLine, endCol, text) {
	return { range: { start: { line, col }, end: { line: endLine, col: endCol } }, text };
}

// A small deterministic generator (mulberry32), so a failure repeats.
function random(seed) {
	let a = seed >>> 0;
	return () => {
		a = (a + 0x6d2b79f5) >>> 0;
		let t = a;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

function positionAt(text, offset) {
	const starts = lineStartsOf(text);
	let line = 0;
	while (line + 1 < starts.length && starts[line + 1] <= offset) line += 1;
	return { line, col: offset - starts[line] };
}

const WORDS = ["a", "bc", "\n", "x\ny", "", "long word", "\u{1F3AE}"];

// Up to three non-overlapping random changes to `text`.
function randomChanges(next, text) {
	const count = 1 + Math.floor(next() * 3);
	const cuts = [];
	for (let i = 0; i < count * 2; i++) cuts.push(Math.floor(next() * (text.length + 1)));
	cuts.sort((x, y) => x - y);
	const out = [];
	for (let i = 0; i < cuts.length; i += 2) {
		let from = cuts[i];
		let to = cuts[i + 1];
		if (text.charCodeAt(from - 1) >= 0xd800 && text.charCodeAt(from - 1) <= 0xdbff) from -= 1;
		if (text.charCodeAt(to - 1) >= 0xd800 && text.charCodeAt(to - 1) <= 0xdbff) to += 1;
		if (out.length && from < out[out.length - 1].to) continue;
		out.push({ from, to, text: WORDS[Math.floor(next() * WORDS.length)] });
	}
	return out.map((c) => ({ range: { start: positionAt(text, c.from), end: positionAt(text, c.to) }, text: c.text }));
}

// One or two edits a few characters wide, as typing makes: they rarely overlap, so remote edits move over pending ones.
function typingChanges(next, text) {
	const out = [];
	let at = 0;
	for (let i = 0; i < 1 + Math.floor(next() * 2); i++) {
		let from = at + Math.floor(next() * Math.max(1, (text.length - at) / 2));
		if (from > text.length) break;
		if (text.charCodeAt(from - 1) >= 0xd800 && text.charCodeAt(from - 1) <= 0xdbff) from += 1;
		let to = Math.min(text.length, from + Math.floor(next() * 3));
		if (text.charCodeAt(to - 1) >= 0xd800 && text.charCodeAt(to - 1) <= 0xdbff) to += 1;
		if (from > text.length || to > text.length) break;
		out.push({ from, to, text: ["k", "\n", "", "zz"][Math.floor(next() * 4)] });
		at = to + 1;
	}
	return out.map((c) => ({ range: { start: positionAt(text, c.from), end: positionAt(text, c.to) }, text: c.text }));
}

test("docsync: the rebase rules meet where both sides insert at one point", () => {
	const server = change(0, 2, 0, 2, "S");
	const mine = change(0, 2, 0, 2, "M");
	const base = "abcd";
	const serverText = applyChanges(applyChanges(base, [server]), rebase([mine], [[server]], transformAfter));
	const clientText = applyChanges(applyChanges(base, [mine]), rebase([server], [[mine]], transformBefore));
	assertEqual(serverText, "abSMcd", "the edit the server applied first goes first");
	assertEqual(clientText, serverText);
});

test("docsync: overlapping changes are refused by both rules", () => {
	let refused = 0;
	for (const rule of [transformAfter, transformBefore]) {
		try {
			rule(change(0, 1, 0, 3, "x"), change(0, 2, 0, 4, "y"));
		}
		catch (e) {
			if (e instanceof OverlapError) refused += 1;
		}
	}
	assertEqual(refused, 2);
});

test("docsync: a random run converges (server rule against client mirror)", () => {
	const next = random(20261020);
	let checked = 0;
	for (let round = 0; round < 2000; round++) {
		const base = Array.from({ length: 1 + Math.floor(next() * 4) }, () => WORDS[Math.floor(next() * WORDS.length)] + "line").join("\n");
		const server = randomChanges(next, base);
		const local0 = randomChanges(next, base);
		const afterLocal0 = applyChanges(base, local0);
		const local1 = randomChanges(next, afterLocal0);
		let serverText;
		let clientText;
		try {
			const sent0 = rebase(local0, [server], transformAfter);
			const remoteOver0 = rebase(server, [local0], transformBefore);
			const queued1 = rebase(local1, [remoteOver0], transformAfter);
			const remoteOver1 = rebase(remoteOver0, [local1], transformBefore);
			serverText = applyChanges(applyChanges(applyChanges(base, server), sent0), queued1);
			clientText = applyChanges(applyChanges(afterLocal0, local1), remoteOver1);
		}
		catch (e) {
			if (e instanceof OverlapError) continue;
			throw e;
		}
		assertEqual(clientText, serverText, "round " + round);
		checked += 1;
	}
	assertTrue(checked > 100, "enough rounds without overlap: " + checked);
});

// ---------------------------------------------------------------------------------------------------------------
// DocumentSync against a model of the server

class ModelServer {
	constructor(text) {
		this.text = text;
		this.version = 1;
		this.history = new Map();
		this.events = [];
		this.holder = "me";
		this.saved = text;
	}

	// Applies an edit made at `baseVersion` as the server does: rebased over what came since, then published.
	edit(baseVersion, changes, origin, opId) {
		const since = [];
		for (let v = baseVersion + 1; v <= this.version; v++) since.push(this.history.get(v));
		const rebased = rebase(changes, since, transformAfter);
		this.text = applyChanges(this.text, rebased);
		this.version += 1;
		this.history.set(this.version, rebased);
		this.events.push({ kind: "doc.edit", data: { version: this.version, changes: rebased, origin, opId, dirty: this.text !== this.saved } });
	}
}

class ModelClient {
	constructor(server) {
		this.server = server;
		this.clientId = "me";
		this.requests = [];
		this.handler = null;
		this.ops = 0;
	}

	newOpId() {
		this.ops += 1;
		return "op-" + this.ops;
	}

	request(method, params, options) {
		return new Promise((resolve, reject) => this.requests.push({ method, params, opId: options && options.opId, resolve, reject }));
	}

	// A second subscription is answered with a snapshot, as the protocol client's resubscription is.
	subscribe(topic, handler) {
		const again = this.handler !== null;
		this.handler = handler;
		if (again) {
			const server = this.server;
			server.events = [];
			queueMicrotask(() => handler.snap({ text: server.text, version: server.version, lease: { holder: server.holder }, dirty: true }, server.version));
		}
		return () => {};
	}

	// The server takes the oldest request; its events and answer are delivered.
	serveOne() {
		const request = this.requests.shift();
		if (request.method === "doc.edit") {
			try {
				this.server.edit(request.params.baseVersion, request.params.changes, "user:me", request.opId);
				request.resolve({ version: this.server.version });
			}
			catch (e) {
				request.reject({ code: "conflict", message: e.message });
			}
		}
		else if (request.method === "doc.save") request.resolve({ version: this.server.version, revision: 2 });
		else if (request.method === "doc.takeLease") {
			this.server.holder = "me";
			request.resolve({ holder: "me" });
		}
	}

	deliver() {
		for (const event of this.server.events.splice(0)) this.handler.evt(event.kind, event.data, event.data.version);
	}
}

class TextTarget {
	constructor() {
		this.text = "";
		this.readOnly = null;
	}

	setText(text) {
		this.text = text;
	}

	applyRemote(changes) {
		this.text = applyChanges(this.text, changes);
	}

	setReadOnly(readOnly) {
		this.readOnly = readOnly;
	}

	// What the editor does when the person types: its text changes, and the sync hears of it.
	type(sync, changes) {
		this.text = applyChanges(this.text, changes);
		sync.localEdit(changes);
	}
}

async function opened(text) {
	const server = new ModelServer(text);
	const client = new ModelClient(server);
	const target = new TextTarget();
	const states = [];
	const sync = new DocumentSync(client, target, { onState: (s) => states.push(s) });
	const opening = sync.open("w1", "a.txt");
	await Promise.resolve();
	client.requests.shift().resolve({ docId: "d1", version: 1, holder: "me" });
	await Promise.resolve();
	await Promise.resolve();
	client.handler.snap({ text, version: 1, lease: { holder: "me" }, dirty: false, languageId: "plaintext" }, 1);
	await opening;
	return { server, client, target, sync, states };
}

// Counts the remote edits a sync moves over pending ones, and whether it ever resynced.
function counting(sync) {
	sync.movedRemote = 0;
	sync.resynced = false;
	const applyRemote = sync.applyRemote.bind(sync);
	sync.applyRemote = (changes) => {
		if (sync.pending > 0) sync.movedRemote += 1;
		applyRemote(changes);
	};
	const resync = sync.resync.bind(sync);
	sync.resync = () => {
		sync.resynced = true;
		resync();
	};
}

async function settle() {
	for (let i = 0; i < 4; i++) await Promise.resolve();
}

test("docsync: edits go one at a time and are acknowledged by their event", async () => {
	const { server, client, target, sync } = await opened("hello\n");
	assertEqual([target.text, target.readOnly], ["hello\n", false], "the snapshot, editable while holding the lease");
	target.type(sync, [change(0, 5, 0, 5, "!")]);
	target.type(sync, [change(0, 6, 0, 6, "?")]);
	assertEqual(client.requests.length, 1, "the second edit waits for the first");
	client.serveOne();
	await settle();
	assertEqual(client.requests.length, 0, "an answer is not the acknowledgement");
	client.deliver();
	assertEqual(client.requests.length, 1, "the event is: the next edit goes");
	assertEqual(client.requests[0].params.baseVersion, 2);
	client.serveOne();
	client.deliver();
	assertEqual([server.text, target.text, sync.pending], ["hello!?\n", "hello!?\n", 0]);
});

test("docsync: undoing back to the saved text is clean, once the server says so", async () => {
	const { client, target, sync } = await opened("hello\n");
	target.type(sync, [change(0, 5, 0, 5, "!")]);
	assertEqual(sync.dirty, true, "dirty as the person types");
	target.type(sync, [change(0, 5, 0, 6, "")]);
	client.serveOne();
	client.deliver();
	assertEqual(sync.dirty, true, "the undo is still on the way");
	client.serveOne();
	client.deliver();
	assertEqual([target.text, sync.dirty], ["hello\n", false], "the server has the file's text again");
});

test("docsync: another writer's edit while this client's are in flight and waiting", async () => {
	const { server, client, target, sync } = await opened("one two three\n");
	target.type(sync, [change(0, 3, 0, 3, "1")]);
	target.type(sync, [change(0, 8, 0, 8, "2")]);
	server.edit(1, [change(0, 0, 0, 0, ">"), change(0, 13, 0, 13, "<")], "ai:c1");
	client.deliver();
	assertEqual(target.text, ">one1 two2 three<\n", "the AI's edit, moved over both of this client's");
	client.serveOne();
	client.deliver();
	client.serveOne();
	client.deliver();
	assertEqual(server.text, target.text, "the server agrees");
	assertEqual(sync.dirty, true);
});

test("docsync: a random interleaving converges", async () => {
	let moved = 0;
	for (let round = 0; round < 360; round++) {
		const next = random(7 + round);
		const { server, client, target, sync } = await opened("alpha\nbeta\ngamma\n");
		counting(sync);
		const log = [];
		const step = async (what, fn) => {
			log.push(what + " | target " + JSON.stringify(target.text) + " server v" + server.version + " " + JSON.stringify(server.text) + " inflight " + JSON.stringify(sync.inflight) + " queue " + JSON.stringify(sync.queue));
			try {
				fn();
			}
			catch (e) {
				throw new Error("round " + round + ": " + e.message + "\n" + log.join("\n"));
			}
			await settle();
		};
		for (let n = 0; n < 12; n++) {
			const roll = next();
			if (roll < 0.4) {
				const changes = typingChanges(next, target.text);
				await step("type " + JSON.stringify(changes), () => target.type(sync, changes));
			}
			else if (roll < 0.6) {
				const changes = typingChanges(next, server.text);
				await step("server " + JSON.stringify(changes), () => server.edit(server.version, changes, "ai:c1"));
			}
			else if (roll < 0.8 && client.requests.length) await step("serve " + JSON.stringify(client.requests[0].params), () => client.serveOne());
			else await step("deliver " + JSON.stringify(server.events), () => client.deliver());
		}
		while (client.requests.length || server.events.length) {
			if (client.requests.length) await step("serve " + JSON.stringify(client.requests[0].params), () => client.serveOne());
			await step("deliver " + JSON.stringify(server.events), () => client.deliver());
		}
		await settle();
		assertEqual([sync.pending, target.text], [0, server.text], "round " + round + ": the replica ends as the server's text");
		if (!sync.resynced) moved += sync.movedRemote;
	}
	assertTrue(moved > 20, "remote edits moved over pending ones without a resync: " + moved);
});

test("docsync: the lease decides who edits", async () => {
	const { client, target, sync } = await opened("x");
	client.handler.evt("doc.lease", { holder: "other" }, 2);
	assertEqual([target.readOnly, sync.holdsLease], [true, false], "read-only once another device holds it");
	const taking = sync.takeLease();
	client.serveOne();
	await taking;
	assertEqual(target.readOnly, false, "editable after taking it over");
});

test("docsync: save waits for pending edits", async () => {
	const { client, target, sync } = await opened("a");
	target.type(sync, [change(0, 1, 0, 1, "b")]);
	let saved = false;
	const saving = sync.save().then(() => (saved = true));
	await settle();
	assertEqual(client.requests.map((r) => r.method), ["doc.edit"], "the edit first");
	client.serveOne();
	client.deliver();
	await settle();
	assertEqual(client.requests.map((r) => r.method), ["doc.save"]);
	client.serveOne();
	await saving;
	assertTrue(saved && !sync.dirty, "then saved");
});

test("docsync: an overlapping remote edit starts over from a snapshot", async () => {
	const { server, client, target, sync, states } = await opened("abcdef");
	target.type(sync, [change(0, 1, 0, 4, "X")]);
	server.edit(1, [change(0, 2, 0, 5, "Y")], "ai:c1");
	client.deliver();
	assertEqual(sync.pending, 0, "the pending edit was dropped");
	assertTrue(states.some((s) => s.notice), "the person is told");
	client.handler.snap({ text: server.text, version: server.version, lease: { holder: "me" }, dirty: true }, server.version);
	assertEqual(target.text, server.text, "the server's text");
});
