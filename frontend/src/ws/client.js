// client.js - protocol v1 (docs/10) for one browser tab: one socket, hello and resume, requests with cancellation and
// opIds, topics with cursors, gap resync, liveness and reconnect with backoff (docs/15 "Connection").
//
// Requests made while the socket is down wait for it. A request in flight when it drops is sent again after the resume
// when it carries an opId (the server answers a duplicate with the first answer); any other fails with `unavailable`.
// Every view of a topic gets {snap(data, seq), evt(kind, data, seq), gone(reason)}; a second view of a topic resubscribes
// it, so both start from a snapshot.

export const PROTOCOL = 1;
const PING_EVERY_MS = 20000;
const PONG_WITHIN_MS = 10000;
const BACKOFF_FIRST_MS = 500;
const BACKOFF_CAP_MS = 15000;
const UNSUBSCRIBE_AFTER_MS = 5000;

export class RpcError extends Error {
	constructor(error) {
		super(error.message || error.code);
		this.code = error.code;
		this.data = error.data;
	}
}

const LOST = { code: "unavailable", message: "the connection to the server was lost" };

class Topic {
	constructor(key) {
		this.key = key;
		this.cursor = 0;
		this.handlers = new Set();
		this.subscribed = false;   // a sub went out on the current socket (or the hello resumed it)
		this.dropTimer = null;
	}
}

export class ProtocolClient {
	// options: url, identity {clientId, deviceId, deviceClass}, openSocket(url), timers {setTimeout, clearTimeout,
	// setInterval, clearInterval, now}, random(), onLeave(reason) for "auth-revoked" / "protocol-mismatch".
	constructor(options) {
		this.url = options.url;
		this.identity = options.identity;
		this.openSocket = options.openSocket || ((url) => new WebSocket(url));
		this.timers = options.timers || defaultTimers();
		this.random = options.random || Math.random;
		this.onLeave = options.onLeave || (() => {});
		this.state = "connecting";
		this.stateListeners = new Set();
		this.socket = null;
		this.welcome = null;
		this.epoch = null;
		this.attempt = 0;
		this.nextId = 1;
		this.pending = new Map();
		this.subRequests = new Map();
		this.topics = new Map();
		this.queue = [];
		this.pingTimer = null;
		this.pongTimer = null;
		this.retryTimer = null;
		this.stopped = false;
	}

	start() {
		this.connect();
	}

	stop() {
		this.stopped = true;
		this.timers.clearTimeout(this.retryTimer);
		this.closeSocket();
		this.setState("closed");
	}

	get clientId() {
		return this.welcome ? this.welcome.clientId : this.identity.clientId;
	}

	onState(fn) {
		this.stateListeners.add(fn);
		return () => this.stateListeners.delete(fn);
	}

	newOpId() {
		return "op-" + randomHex(12);
	}

	// A method call: resolves with the result, rejects with RpcError. options: {opId, signal}.
	request(method, params, options) {
		const opts = options || {};
		return new Promise((resolve, reject) => {
			const call = { id: this.nextId++, method, params: params || {}, opId: opts.opId, resolve, reject, sent: false };
			if (opts.signal) {
				if (opts.signal.aborted) return reject(new RpcError({ code: "cancelled", message: "cancelled" }));
				opts.signal.addEventListener("abort", () => this.cancel(call), { once: true });
			}
			this.pending.set(call.id, call);
			if (this.state === "live") this.sendCall(call);
			else this.queue.push(call);
		});
	}

	// Views of `topic`: handler {snap, evt, gone}. Returns the function that removes this view.
	subscribe(key, handler) {
		let topic = this.topics.get(key);
		if (!topic) {
			topic = new Topic(key);
			this.topics.set(key, topic);
		}
		this.timers.clearTimeout(topic.dropTimer);
		topic.dropTimer = null;
		topic.handlers.add(handler);
		if (this.state === "live") this.sendSub([topic]);
		return () => this.unsubscribe(topic, handler);
	}

	unsubscribe(topic, handler) {
		topic.handlers.delete(handler);
		if (topic.handlers.size > 0 || topic.dropTimer) return;
		topic.dropTimer = this.timers.setTimeout(() => {
			topic.dropTimer = null;
			if (topic.handlers.size > 0) return;
			this.topics.delete(topic.key);
			if (this.state === "live" && topic.subscribed) this.send({ t: "unsub", topics: [topic.key] });
		}, UNSUBSCRIBE_AFTER_MS);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The socket

	connect() {
		if (this.stopped) return;
		const socket = this.openSocket(this.url);
		this.socket = socket;
		socket.onopen = () => this.sendHello(socket);
		socket.onmessage = (event) => this.receive(socket, event.data);
		socket.onclose = () => this.lost(socket);
		socket.onerror = () => {};
	}

	sendHello(socket) {
		if (socket !== this.socket) return;
		const hello = { t: "hello", proto: PROTOCOL, clientId: this.identity.clientId, deviceId: this.identity.deviceId, deviceClass: this.identity.deviceClass };
		const cursors = this.resumableCursors();
		if (this.epoch && Object.keys(cursors).length) hello.resume = { sessionEpoch: this.epoch, cursors };
		this.send(hello);
	}

	resumableCursors() {
		const cursors = {};
		for (const topic of this.topics.values()) if (topic.subscribed && topic.cursor > 0) cursors[topic.key] = topic.cursor;
		return cursors;
	}

	receive(socket, text) {
		if (socket !== this.socket) return;
		let frame;
		try {
			frame = JSON.parse(text);
		}
		catch {
			return;
		}
		this.armPong();
		const handle = FRAME_HANDLERS[frame.t];
		if (handle) handle(this, frame);
	}

	welcomed(frame) {
		const resumed = frame.resume || {};
		const restarted = this.epoch !== null && frame.sessionEpoch !== this.epoch;
		this.welcome = frame;
		this.epoch = frame.sessionEpoch;
		this.attempt = 0;
		this.setState("live");
		this.startPings();
		const fresh = [];
		for (const topic of this.topics.values()) {
			const wasResumed = topic.subscribed && topic.cursor > 0;
			if (restarted) topic.cursor = 0;
			if (wasResumed && !(topic.key in resumed)) this.drop(topic, "gone");
			else if (!wasResumed) fresh.push(topic);
		}
		if (fresh.length) this.sendSub(fresh);
		const waiting = this.queue;
		this.queue = [];
		for (const call of waiting) if (this.pending.has(call.id)) this.sendCall(call);
	}

	lost(socket) {
		if (socket !== this.socket) return;
		this.socket = null;
		this.stopPings();
		for (const topic of this.topics.values()) if (!(topic.subscribed && topic.cursor > 0)) topic.subscribed = false;
		for (const call of [...this.pending.values()]) {
			if (!call.sent) continue;
			if (call.opId) {
				call.sent = false;
				this.queue.push(call);
			}
			else this.fail(call, LOST);
		}
		this.subRequests.clear();
		if (this.stopped) return;
		this.setState(this.welcome ? "reconnecting" : "connecting");
		const delay = Math.min(BACKOFF_CAP_MS, BACKOFF_FIRST_MS * 2 ** this.attempt) * (0.75 + 0.5 * this.random());
		this.attempt += 1;
		this.retryTimer = this.timers.setTimeout(() => this.connect(), delay);
	}

	// Connects at once when it waits to reconnect - its server is known to be back (a workspace server the master says is
	// ready, docs/10 "Routing") - instead of after the rest of its backoff.
	reconnectNow() {
		if (this.stopped || this.socket || !this.retryTimer) return;
		this.timers.clearTimeout(this.retryTimer);
		this.retryTimer = null;
		this.connect();
	}

	closeSocket() {
		const socket = this.socket;
		this.socket = null;
		this.stopPings();
		if (socket) {
			try {
				socket.close();
			}
			catch {
				// already closed
			}
		}
	}

	// A socket that stopped answering: closed, then the reconnect path runs as for any loss.
	dead() {
		const socket = this.socket;
		if (!socket) return;
		try {
			socket.close();
		}
		catch {
			// already closed
		}
		this.lost(socket);
	}

	startPings() {
		this.stopPings();
		this.pingTimer = this.timers.setInterval(() => {
			this.send({ t: "ping", ts: this.timers.now() });
			if (!this.pongTimer) this.pongTimer = this.timers.setTimeout(() => this.dead(), PONG_WITHIN_MS);
		}, PING_EVERY_MS);
	}

	// Any frame proves the socket alive.
	armPong() {
		this.timers.clearTimeout(this.pongTimer);
		this.pongTimer = null;
	}

	stopPings() {
		this.timers.clearInterval(this.pingTimer);
		this.timers.clearTimeout(this.pongTimer);
		this.pingTimer = null;
		this.pongTimer = null;
	}

	send(frame) {
		if (!this.socket) return false;
		this.socket.send(JSON.stringify(frame));
		return true;
	}

	setState(state) {
		if (this.state === state) return;
		this.state = state;
		for (const fn of [...this.stateListeners]) fn(state);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Requests

	sendCall(call) {
		const frame = { t: "req", id: call.id, method: call.method, params: call.params };
		if (call.opId) frame.opId = call.opId;
		call.sent = this.send(frame);
		if (!call.sent) this.queue.push(call);
	}

	cancel(call) {
		if (!this.pending.has(call.id)) return;
		if (call.sent) this.send({ t: "cancel", id: call.id });
		this.fail(call, { code: "cancelled", message: "cancelled" });
	}

	answered(frame) {
		const sub = this.subRequests.get(frame.id);
		if (sub) {
			this.subRequests.delete(frame.id);
			this.subAnswered(sub, frame);
			return;
		}
		const call = this.pending.get(frame.id);
		if (!call) return;
		this.pending.delete(frame.id);
		if (frame.ok) call.resolve(frame.result);
		else call.reject(new RpcError(frame.error || LOST));
	}

	fail(call, error) {
		this.pending.delete(call.id);
		call.reject(new RpcError(error));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Topics

	sendSub(topics, from) {
		const id = this.nextId++;
		const frame = { t: "sub", id, topics: topics.map((topic) => topic.key) };
		if (from) frame.from = from;
		if (!this.send(frame)) return;
		for (const topic of topics) topic.subscribed = true;
		this.subRequests.set(id, topics);
	}

	subAnswered(topics, frame) {
		const statuses = frame.ok && frame.result ? frame.result.topics || {} : {};
		for (const topic of topics) {
			const status = frame.ok ? statuses[topic.key] : "error";
			if (status === "not_found" || status === "error") this.drop(topic, status);
		}
	}

	drop(topic, reason) {
		this.topics.delete(topic.key);
		this.timers.clearTimeout(topic.dropTimer);
		for (const handler of [...topic.handlers]) if (handler.gone) handler.gone(reason);
	}

	snapshot(frame) {
		const topic = this.topics.get(frame.topic);
		if (!topic) return;
		topic.cursor = frame.seq;
		for (const handler of [...topic.handlers]) if (handler.snap) handler.snap(frame.data, frame.seq);
	}

	event(frame) {
		const topic = this.topics.get(frame.topic);
		if (!topic || frame.seq <= topic.cursor) return;
		topic.cursor = frame.seq;
		for (const handler of [...topic.handlers]) if (handler.evt) handler.evt(frame.kind, frame.data, frame.seq);
	}

	// The server dropped events for a slow client: subscribe again from the cursor (replay or snapshot, its choice).
	gap(frame) {
		const topic = this.topics.get(frame.topic);
		if (!topic) return;
		this.sendSub([topic], { [topic.key]: topic.cursor });
	}

	left(frame) {
		if (frame.reason === "auth-revoked" || frame.reason === "protocol-mismatch") {
			this.stop();
			this.onLeave(frame.reason);
		}
	}
}

const FRAME_HANDLERS = {
	welcome: (client, frame) => client.welcomed(frame),
	res: (client, frame) => client.answered(frame),
	snap: (client, frame) => client.snapshot(frame),
	evt: (client, frame) => client.event(frame),
	gap: (client, frame) => client.gap(frame),
	bye: (client, frame) => client.left(frame),
	pong: () => {},
};

function defaultTimers() {
	return {
		setTimeout: (fn, ms) => setTimeout(fn, ms),
		clearTimeout: (id) => clearTimeout(id),
		setInterval: (fn, ms) => setInterval(fn, ms),
		clearInterval: (id) => clearInterval(id),
		now: () => Date.now(),
	};
}

export function randomHex(count) {
	const bytes = new Uint8Array(Math.ceil(count / 2));
	crypto.getRandomValues(bytes);
	return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("").slice(0, count);
}
