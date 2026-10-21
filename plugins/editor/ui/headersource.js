// headersource.js - Toggle Header / Code File (VS Ctrl+K, Ctrl+O): the file with the same name and a paired extension,
// beside the document or in one of the pair's searchPaths, from the plugins' `headerSourcePairs` contributions
// ({extensions: [".h", ".c", ...], searchPaths: ["../include"]}). The host knows no language: the pairs do.

function extensionOf(path) {
	const slash = path.lastIndexOf("/");
	const dot = path.lastIndexOf(".");
	return dot > slash ? path.slice(dot).toLowerCase() : "";
}

// `relative` ("../include") applied to the folder `folder` ("src/game"); null when it climbs out of the workspace.
export function joinFolder(folder, relative) {
	const parts = folder ? folder.split("/") : [];
	for (const part of relative.split("/")) {
		if (!part || part === ".") continue;
		if (part === "..") {
			if (!parts.length) return null;
			parts.pop();
		}
		else parts.push(part);
	}
	return parts.join("/");
}

// The paths that would be `path`'s counterpart, in the order to try: each paired extension beside it, then in each
// search path. `pairs` are the contributions' values.
export function counterparts(path, pairs) {
	const extension = extensionOf(path);
	if (!extension) return [];
	const slash = path.lastIndexOf("/");
	const folder = slash < 0 ? "" : path.slice(0, slash);
	const stem = path.slice(slash + 1, path.length - extension.length);
	const out = [];
	for (const pair of pairs) {
		const extensions = (pair.extensions || []).map((e) => e.toLowerCase());
		if (!extensions.includes(extension)) continue;
		const folders = [folder].concat((pair.searchPaths || []).map((p) => joinFolder(folder, p)).filter((f) => f !== null));
		for (const where of folders)
			for (const other of extensions)
				if (other !== extension) out.push((where ? where + "/" : "") + stem + other);
	}
	return [...new Set(out)];
}

// The first counterpart that exists, by listing the folders it could be in; null for none.
export async function findCounterpart(client, wsId, path, pairs) {
	const listed = new Map();
	for (const candidate of counterparts(path, pairs)) {
		const slash = candidate.lastIndexOf("/");
		const folder = slash < 0 ? "" : candidate.slice(0, slash);
		if (!listed.has(folder)) {
			try {
				const answer = await client.request("fs.list", { wsId, path: folder });
				listed.set(folder, new Map(answer.entries.filter((e) => e.kind === "file").map((e) => [e.name.toLowerCase(), e.name])));
			}
			catch (e) {
				listed.set(folder, new Map());
			}
		}
		const name = listed.get(folder).get(candidate.slice(slash + 1).toLowerCase());
		if (name) return (folder ? folder + "/" : "") + name;
	}
	return null;
}
