// paths.js - workspace paths (docs/10): how a relative path written in one file is read from that file's folder -
// the Markdown preview's links and images, and the editor's, are all read this way.

// The workspace path `href` names, seen from the folder `from` ("" for the root); null when it leaves the workspace.
export function resolveRelative(from, href) {
	const parts = from ? from.split("/") : [];
	for (const part of href.split("/")) {
		if (part === "" || part === ".") continue;
		if (part === "..") {
			if (!parts.length) return null;
			parts.pop();
		}
		else parts.push(part);
	}
	return parts.join("/");
}
