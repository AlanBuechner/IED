// fileurl.js - a workspace file over HTTP (docs/10 "GET /w/<wsId>/files/<path>"): its URL, its bytes or a range of
// them, and a call each time it changes on disk (the workspace's tree.changed).
//     const { bytes, size } = await fetchRange(fileUrl("w1", "bin/game.exe"), 0, 65535);

export function fileUrl(wsId, path, options) {
	const url = "/w/" + encodeURIComponent(wsId) + "/files/" + path.split("/").map(encodeURIComponent).join("/");
	return options && options.download ? url + "?download=1" : url;
}

async function problemOf(answer) {
	const text = (await answer.text().catch(() => "")).trim();
	return answer.status === 404 ? "The file is not there." : "The server answered " + answer.status + (text ? ": " + text : ".");
}

// The file's size from a Content-Range ("bytes 0-99/1234", "bytes */1234").
export function totalOf(contentRange) {
	const match = /\/(\d+)\s*$/.exec(contentRange || "");
	return match ? Number(match[1]) : -1;
}

// {bytes, size}: the file's bytes `first`..`last` (inclusive; fewer when the file ends, or the server sends at most 4 MiB
// a range), and the file's size.
export async function fetchRange(url, first, last, signal) {
	const answer = await fetch(url, { headers: { Range: "bytes=" + first + "-" + last }, cache: "no-store", signal });
	if (answer.status === 416) return { bytes: new Uint8Array(0), size: totalOf(answer.headers.get("Content-Range")) };
	if (!answer.ok) throw new Error(await problemOf(answer));
	const bytes = new Uint8Array(await answer.arrayBuffer());
	if (answer.status === 200) return { bytes: bytes.subarray(first, last + 1), size: bytes.length };
	return { bytes, size: totalOf(answer.headers.get("Content-Range")) };
}

// The whole file's bytes.
export async function fetchFile(url, signal) {
	const answer = await fetch(url, { cache: "no-store", signal });
	if (!answer.ok) throw new Error(await problemOf(answer));
	return new Uint8Array(await answer.arrayBuffer());
}

// Calls `changed(kind)` - "changed", or "deleted" - when the file at `path` changes; returns the unsubscribe.
export function watchFile(ied, wsId, path, changed) {
	return ied.client.subscribe("ws:" + wsId, {
		evt: (kind, data) => {
			if (kind !== "tree.changed") return;
			if (data.overflow) return changed("changed");
			const change = (data.changes || []).find((c) => c.path === path);
			if (change) changed(change.change === "deleted" ? "deleted" : "changed");
			return undefined;
		},
	});
}
