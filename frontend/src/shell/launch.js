// launch.js - what a launch URL asks the web client to show (docs/10 control `open`): "#open=<wsId>/<path>[:<line>[:<col>]]".

// {wsId, path, line, col} of the fragment (line and col 1-based, 0 when absent), or null for another fragment.
export function parseOpenFragment(hash) {
	const match = /^#?open=([^/]+)\/(.*?)(?::(\d+))?(?::(\d+))?$/.exec(hash || "");
	if (!match) return null;
	let path;
	try {
		path = decodeURIComponent(match[2]);
	}
	catch {
		path = match[2];
	}
	return { wsId: decodeURIComponent(match[1]), path, line: match[3] ? Number(match[3]) : 0, col: match[4] ? Number(match[4]) : 0 };
}
