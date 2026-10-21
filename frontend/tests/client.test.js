// client.test.js - the protocol client against a fake socket and fake timers: hello, requests, topics and cursors,
// gaps, resume after a drop, a server restart, opId resends, cancellation, liveness, backoff, bye.

import { test, assertEqual, assertTrue } from "./harness.js";
import { ProtocolClient, RpcError } from "../src/ws/client.js";

class FakeTimers {
	constructor() {
		this.time = 0;
		this.nextId = 1;
		this.items = new Map();
	}

	setTimeout(fn, ms) {
		const id = this.nextId++;
		this.items.set(id, { fn, at: this.time + ms });
		return id;
	}

	setInterval(fn, ms) {
		const id = this.nextId++;
		this.items.set(id, { fn, at: this.time + ms, every: ms });
		return id;
	}

	clearTimeout(id) {
		this.items.delete(id);
	}

	clearInterval(id) {
		this.items.delete(id);
	}

	now() {
		return this.time;
	}

	advance(ms) {
		const end = this.time + ms;
		for (;;) {
			const due = [...this.items.entries()].filter(([, item]) => item.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
			if (!due) break;
			const [id, item] = due;
			this.time = item.at;
			if (item.every) item.at += item.every;
			else this.items.delete(id);
			item.fn();
		}
		this.time = end;
	}
}

class FakeSocket {
	constructor() {
		this.sent = [];
		this.closed = false;
	}

	send(text) {
		this.sent.push(JSON.parse(text));
	}

	close() {
		if (this.closed) return;
		this.closed = true;
		this.onclose && this.onclose({});
	}

	open() {
		this.onopen && this.onopen({});
	}

	deliver(frame) {
		this.onmessage && this.onmessage({ data: JSON.stringify(frame) });
	}

	drop() {
		this.close();
	}

	frames(t) {
		return this.sent.filter((f) => f.t === t);
	}
}

function setup(options) {
	const timers = new FakeTimers();
	const sockets = [];
	const left = [];
	const client = new ProtocolClient(Object.assign({
		url: "ws://test/ws",
		identity: { clientId: "c-1", deviceId: "d-1", deviceClass: "desktop" },
		openSocket: () => {
			const socket = new FakeSocket();
			sockets.push(socket);
			return socket;
		},
		timers,
		random: () => 0.5,
		onLeave: (reason) => left.push(reason),
	}, options || {}));
	client.start();
	return { client, timers, sockets, left, socket: () => sockets[sockets.length - 1] };
}

function welcome(socket, epoch, resume) {
	socket.deliver({ t: "welcome", proto: 1, serverVersion: "test", sessionEpoch: epoch || "e-1", clientId: "c-1", resume: resume || {} });
}

function live() {
	const env = setup();
	env.socket().open();
	welcome(env.socket());
	return env;
}

function recorder() {
	const seen = [];
	return { seen, handler: { snap: (data, seq) => seen.push(["snap", data, seq]), evt: (kind, data, seq) => seen.push([kind, data, seq]), gone: (reason) => seen.push(["gone", reason]) } };
}

async function settle() {
	await Promise.resolve();
	await Promise.resolve();
}

test("client: hello first, then live", () => {
	const env = setup();
	const states = [];
	env.client.onState((s) => states.push(s));
	assertEqual(env.client.state, "connecting");
	env.socket().open();
	assertEqual(env.socket().sent, [{ t: "hello", proto: 1, clientId: "c-1", deviceId: "d-1", deviceClass: "desktop" }], "no resume on a first connect");
	welcome(env.socket());
	assertEqual(states, ["live"]);
});

test("client: requests made before the welcome wait for it", async () => {
	const env = setup();
	const answer = env.client.request("ws.list", {});
	env.socket().open();
	assertEqual(env.socket().frames("req").length, 0);
	welcome(env.socket());
	const req = env.socket().frames("req")[0];
	assertEqual([req.method, req.params], ["ws.list", {}]);
	env.socket().deliver({ t: "res", id: req.id, ok: true, result: { workspaces: [] } });
	assertEqual(await answer, { workspaces: [] });
});

test("client: an error answer rejects with its code and data", async () => {
	const env = live();
	const answer = env.client.request("fs.write", { path: "a" });
	const req = env.socket().frames("req")[0];
	env.socket().deliver({ t: "res", id: req.id, ok: false, error: { code: "conflict", message: "changed", data: { revision: 4 } } });
	try {
		await answer;
		assertTrue(false, "should reject");
	}
	catch (e) {
		assertTrue(e instanceof RpcError);
		assertEqual([e.code, e.data], ["conflict", { revision: 4 }]);
	}
});

test("client: topics deliver snapshots and events in order, never twice", () => {
	const env = live();
	const { seen, handler } = recorder();
	env.client.subscribe("out:w1:ied", handler);
	const sub = env.socket().frames("sub")[0];
	assertEqual(sub.topics, ["out:w1:ied"]);
	env.socket().deliver({ t: "res", id: sub.id, ok: true, result: { topics: { "out:w1:ied": "snapshot" } } });
	env.socket().deliver({ t: "snap", topic: "out:w1:ied", seq: 5, data: { tail: "a" } });
	env.socket().deliver({ t: "evt", topic: "out:w1:ied", seq: 6, kind: "append", data: { text: "b" } });
	env.socket().deliver({ t: "evt", topic: "out:w1:ied", seq: 6, kind: "append", data: { text: "b" } });
	env.socket().deliver({ t: "evt", topic: "out:w1:ied", seq: 4, kind: "append", data: { text: "old" } });
	env.socket().deliver({ t: "evt", topic: "other", seq: 9, kind: "x", data: {} });
	assertEqual(seen, [["snap", { tail: "a" }, 5], ["append", { text: "b" }, 6]]);
});

test("client: a second view of a topic subscribes it again", () => {
	const env = live();
	env.client.subscribe("ws:w1", recorder().handler);
	env.client.subscribe("ws:w1", recorder().handler);
	assertEqual(env.socket().frames("sub").map((f) => f.topics), [["ws:w1"], ["ws:w1"]]);
});

test("client: a topic no view wants is unsubscribed later, unless a view comes back", () => {
	const env = live();
	const leave = env.client.subscribe("ws:w1", recorder().handler);
	leave();
	env.timers.advance(4000);
	const back = env.client.subscribe("ws:w1", recorder().handler);
	env.timers.advance(10000);
	assertEqual(env.socket().frames("unsub").length, 0, "came back in time");
	back();
	env.timers.advance(5000);
	assertEqual(env.socket().frames("unsub"), [{ t: "unsub", topics: ["ws:w1"] }]);
});

test("client: a topic the server does not have is gone", () => {
	const env = live();
	const { seen, handler } = recorder();
	env.client.subscribe("doc:missing", handler);
	const sub = env.socket().frames("sub")[0];
	env.socket().deliver({ t: "res", id: sub.id, ok: true, result: { topics: { "doc:missing": "not_found" } } });
	assertEqual(seen, [["gone", "not_found"]]);
});

test("client: a gap subscribes again from the cursor", () => {
	const env = live();
	env.client.subscribe("run:r1", recorder().handler);
	env.socket().deliver({ t: "snap", topic: "run:r1", seq: 10, data: {} });
	env.socket().deliver({ t: "evt", topic: "run:r1", seq: 11, kind: "output", data: {} });
	env.socket().deliver({ t: "gap", topic: "run:r1", from: 12, to: 40 });
	assertEqual(env.socket().frames("sub")[1].from, { "run:r1": 11 });
});

test("client: after a drop it resumes from its cursors and resends opId requests", async () => {
	const env = live();
	const states = [];
	env.client.onState((s) => states.push(s));
	const { seen, handler } = recorder();
	env.client.subscribe("chat:c1", handler);
	env.client.subscribe("doc:d1", recorder().handler);
	env.socket().deliver({ t: "snap", topic: "chat:c1", seq: 20, data: {} });
	env.socket().deliver({ t: "evt", topic: "chat:c1", seq: 21, kind: "segment.delta", data: { text: "a" } });
	const send = env.client.request("chat.send", { text: "hi" }, { opId: "op-1" });
	const list = env.client.request("ws.list", {});
	const first = env.socket();
	first.drop();
	assertEqual(states, ["reconnecting"]);
	let failure = null;
	try {
		await list;
	}
	catch (e) {
		failure = e;
	}
	assertEqual(failure && failure.code, "unavailable", "a request without an opId fails");
	env.timers.advance(400);
	assertEqual(env.sockets.length, 1, "backoff: 0.5 s at the first attempt");
	env.timers.advance(100);
	assertEqual(env.sockets.length, 2);
	env.socket().open();
	const hello = env.socket().frames("hello")[0];
	assertEqual(hello.resume, { sessionEpoch: "e-1", cursors: { "chat:c1": 21 } }, "only topics with a cursor resume");
	welcome(env.socket(), "e-1", { "chat:c1": "ok" });
	const resent = env.socket().frames("req")[0];
	assertEqual([resent.method, resent.opId], ["chat.send", "op-1"], "the opId request is sent again");
	assertEqual(env.socket().frames("sub").map((f) => f.topics), [["doc:d1"]], "a topic without a snapshot yet is subscribed again");
	env.socket().deliver({ t: "evt", topic: "chat:c1", seq: 21, kind: "segment.delta", data: { text: "a" } });
	env.socket().deliver({ t: "evt", topic: "chat:c1", seq: 22, kind: "segment.delta", data: { text: "b" } });
	assertEqual(seen.slice(-1), [["segment.delta", { text: "b" }, 22]], "replayed events are applied once");
	env.socket().deliver({ t: "res", id: resent.id, ok: true, result: { turnId: "u1" } });
	assertEqual(await send, { turnId: "u1" });
});

test("client: a topic the resume no longer names is gone", () => {
	const env = live();
	const { seen, handler } = recorder();
	env.client.subscribe("run:r9", handler);
	env.socket().deliver({ t: "snap", topic: "run:r9", seq: 3, data: {} });
	env.socket().drop();
	env.timers.advance(1000);
	env.socket().open();
	welcome(env.socket(), "e-1", {});
	assertEqual(seen.slice(-1), [["gone", "gone"]]);
});

test("client: after a server restart snapshots start over", () => {
	const env = live();
	const { seen, handler } = recorder();
	env.client.subscribe("ws:w1", handler);
	env.socket().deliver({ t: "snap", topic: "ws:w1", seq: 50, data: { a: 1 } });
	env.socket().drop();
	env.timers.advance(1000);
	env.socket().open();
	welcome(env.socket(), "e-2", { "ws:w1": "snapshot" });
	env.socket().deliver({ t: "snap", topic: "ws:w1", seq: 50, data: { a: 2 } });
	assertEqual(seen.slice(-1), [["snap", { a: 2 }, 50]], "a snapshot at the old seq still applies after a new epoch");
});

test("client: backoff doubles to a cap and resets once live", () => {
	const env = setup();
	const delays = [];
	for (let i = 0; i < 7; i++) {
		const before = env.timers.time;
		const count = env.sockets.length;
		env.socket().drop();
		while (env.sockets.length === count) env.timers.advance(50);
		delays.push(env.timers.time - before);
	}
	assertEqual(delays, [500, 1000, 2000, 4000, 8000, 15000, 15000]);
	env.socket().open();
	welcome(env.socket());
	const before = env.timers.time;
	env.socket().drop();
	while (env.sockets.length === 8) env.timers.advance(50);
	assertEqual(env.timers.time - before, 500, "reset after a welcome");
});

test("client: a socket that stops answering pings is dropped", () => {
	const env = live();
	env.timers.advance(20000);
	assertEqual(env.socket().frames("ping").length, 1);
	env.timers.advance(9000);
	env.socket().deliver({ t: "pong", ts: 1 });
	env.timers.advance(5000);
	assertTrue(!env.socket().closed, "a pong arrived in time");
	env.timers.advance(16000);
	env.timers.advance(10000);
	assertTrue(env.sockets[0].closed, "no answer within 10 s");
	assertEqual(env.client.state, "reconnecting");
});

test("client: cancelling a request", async () => {
	const env = live();
	const abort = new AbortController();
	const answer = env.client.request("fs.search", { query: "x" }, { signal: abort.signal });
	const req = env.socket().frames("req")[0];
	abort.abort();
	assertEqual(env.socket().frames("cancel"), [{ t: "cancel", id: req.id }]);
	let code = null;
	try {
		await answer;
	}
	catch (e) {
		code = e.code;
	}
	assertEqual(code, "cancelled");
	env.socket().deliver({ t: "res", id: req.id, ok: false, error: { code: "cancelled", message: "x" } });
	await settle();
});

test("client: bye for auth or a protocol mismatch leaves; a restart bye reconnects", () => {
	const env = live();
	env.socket().deliver({ t: "bye", reason: "server-restart" });
	env.socket().drop();
	assertEqual(env.left, []);
	assertEqual(env.client.state, "reconnecting");
	env.timers.advance(1000);
	env.socket().open();
	welcome(env.socket());
	env.socket().deliver({ t: "bye", reason: "auth-revoked" });
	assertEqual(env.left, ["auth-revoked"]);
	assertEqual(env.client.state, "closed");
	env.timers.advance(60000);
	assertEqual(env.sockets.length, 2, "no reconnect once it left");
});
