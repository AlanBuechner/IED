// fileuris.js - files outside the workspace (docs/10 "Files outside the workspace") are documents named by their
// `file:` URI, in the one form the server gives it (src/lsp/uris.cf CanonicalFileUri): the path with '/' separators and
// an upper-case drive letter, its UTF-8 bytes percent-encoded but for letters, digits and "-._~/:". Monaco writes the
// same URI another way ("file:///c%3A/..."), so a tab and a document are always named by this form.

export function isFileUri(text) {
	return typeof text === "string" && /^file:\/\//i.test(text);
}

function pathOf(uri) {
	let path = decodeURIComponent(uri.slice("file://".length)).replace(/\\/g, "/");
	if (/^\/[a-zA-Z]:/.test(path)) path = path.slice(1);
	if (/^[a-z]:/.test(path)) path = path[0].toUpperCase() + path.slice(1);
	return path;
}

// The canonical form of a file: URI, or null for anything else (or a bad escape).
export function canonicalFileUri(uri) {
	if (!isFileUri(uri)) return null;
	let path;
	try {
		path = pathOf(uri);
	}
	catch (e) {
		return null;
	}
	let encoded = "";
	for (const byte of new TextEncoder().encode(path)) {
		const c = String.fromCharCode(byte);
		encoded += /^[A-Za-z0-9\-._~/:]$/.test(c) ? c : "%" + byte.toString(16).toUpperCase().padStart(2, "0");
	}
	return "file://" + (encoded.startsWith("/") ? "" : "/") + encoded;
}

// The path of a file: URI as the person reads it ("C:/Program Files/LLVM/include/stddef.h").
export function displayPath(uri) {
	try {
		return pathOf(uri);
	}
	catch (e) {
		return uri;
	}
}

// A document path's file name: a workspace path's last segment, or a file URI's, decoded.
export function fileName(path) {
	const name = path.slice(path.lastIndexOf("/") + 1);
	if (!isFileUri(path)) return name;
	try {
		return decodeURIComponent(name);
	}
	catch (e) {
		return name;
	}
}
