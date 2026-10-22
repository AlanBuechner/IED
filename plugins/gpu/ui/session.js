// session.js - a capture's replay session as the UI uses it (docs/22 "Server and protocol"): gpu.open starts or joins
// the server's replay of the capture, call() sends one ied-gpu/1 request through gpu.replay and fetches its binary
// part (a blob) as an ArrayBuffer. The session's state comes from gpurs:<sessionId>; a session that failed (the
// replayer exited) is opened again by the next call.

export class ReplaySession {
	constructor(ied, wsId, captureId, { onState, onProgress } = {}) {
		this.ied = ied;
		this.wsId = wsId;
		this.captureId = captureId;
		this.onState = onState || (() => {});
		this.onProgress = onProgress || (() => {});
		this.sessionId = null;
		this.summary = null;
		this.state = "starting";
		this.stop = null;
		this.opening = null;
	}

	// Opens (or joins) the replay; resolves with initialize's summary.
	open() {
		if (this.opening) return this.opening;
		this.opening = this.ied.client.request("gpu.open", { wsId: this.wsId, captureId: this.captureId }).then((session) => {
			this.follow(session);
			return this.summary;
		}).finally(() => {
			this.opening = null;
		});
		return this.opening;
	}

	follow(session) {
		if (this.stop) this.stop();
		this.sessionId = session.sessionId;
		this.apply(session);
		this.stop = this.ied.client.subscribe("gpurs:" + session.sessionId, {
			snap: (data) => this.apply(data),
			evt: (kind, data) => {
				if (kind === "state") this.apply(data);
				else if (kind === "progress") this.onProgress(data);
			},
		});
	}

	apply(data) {
		if (!data) return;
		this.state = data.state;
		if (data.summary) this.summary = data.summary;
		this.compareWith = data.compareWith || "";
		this.edits = data.edits || [];
		this.problem = data.problem || "";
		this.onState(this);
	}

	// One request: {result, binary} (binary an ArrayBuffer, or null).
	async call(method, params) {
		if (!this.sessionId || this.state === "failed" || this.state === "ended") await this.open();
		const reply = await this.ied.client.request("gpu.replay", { sessionId: this.sessionId, method, params: params || {} });
		let binary = null;
		if (reply.blob) {
			const response = await fetch("/api/blob/" + encodeURIComponent(reply.blob), { credentials: "same-origin" });
			if (!response.ok) throw new Error("the replay's data could not be fetched (" + response.status + ")");
			binary = await response.arrayBuffer();
		}
		return { result: reply.result, binary };
	}

	dispose() {
		if (this.stop) this.stop();
		this.stop = null;
	}
}
