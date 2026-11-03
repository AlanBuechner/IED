// launch.js - what a launch URL asks the web client to show (docs/10 control `open`): "#open=<wsId>/<path>[:<line>[:<col>]]",
// with "?win=<id>" after it when the desktop app brings a docked window back (docs/27): sign-in drops a URL's query, a
// fragment stays. A path's own "?" is percent-encoded.

// {wsId, path, line, col} of the fragment (line and col 1-based, 0 when absent), and {win} when it names a window; null for
// another fragment.
export function parseOpenFragment(hash) {
	const text = hash || "";
	const query = text.indexOf("?");
	const match = /^#?open=([^/]+)\/(.*?)(?::(\d+))?(?::(\d+))?$/.exec(query < 0 ? text : text.slice(0, query));
	if (!match) return null;
	let path;
	try {
		path = decodeURIComponent(match[2]);
	}
	catch {
		path = match[2];
	}
	const open = { wsId: decodeURIComponent(match[1]), path, line: match[3] ? Number(match[3]) : 0, col: match[4] ? Number(match[4]) : 0 };
	const params = new URLSearchParams(query < 0 ? "" : text.slice(query + 1));
	if (params.get("win")) open.win = params.get("win");
	return open;
}
