// fileicons.js - the editor's default file-type icons (docs/15 "Icons"): a codicon per file category with the category's
// identity color (theme tokens --filetype-*). Language plugins add their extensions with `fileIcons` (M2).

import { icon } from "/ui-kit/dom.js";

const CATEGORIES = [
	["source", "file-code", ["c", "cc", "cpp", "cxx", "cf", "cs", "go", "rs", "py", "js", "mjs", "ts", "tsx", "jsx", "java", "kt", "swift", "m", "mm", "lua", "zig"]],
	["header", "file-code", ["h", "hh", "hpp", "hxx", "ch", "inl", "ipp"]],
	["data", "json", ["json", "jsonc", "toml", "yaml", "yml", "xml", "ini", "csv", "sln", "proj", "lock"]],
	["doc", "markdown", ["md", "markdown", "rst", "txt", "adoc"]],
	["script", "terminal", ["ps1", "psm1", "sh", "bash", "bat", "cmd", "zsh", "fish"]],
	["shader", "symbol-color", ["hlsl", "hlsli", "glsl", "vert", "frag", "comp", "geom", "wgsl", "shader", "usf", "ush", "fx"]],
	["media", "file-media", ["png", "jpg", "jpeg", "gif", "bmp", "tga", "dds", "svg", "ico", "wav", "mp3", "ogg", "flac", "ttf", "otf", "woff", "woff2", "mp4", "webm"]],
	["web", "file-code", ["html", "htm", "css", "scss", "less", "vue", "svelte"]],
];

const BY_EXTENSION = new Map();
for (const [category, glyph, extensions] of CATEGORIES) for (const extension of extensions) BY_EXTENSION.set(extension, [category, glyph]);

export function extensionOf(name) {
	const dot = name.lastIndexOf(".");
	return dot > 0 ? name.slice(dot + 1).toLowerCase() : "";
}

export function fileIcon(name) {
	const [category, glyph] = BY_EXTENSION.get(extensionOf(name)) || ["plain", "file"];
	return icon(glyph, "ft-" + (category === "web" ? "source" : category));
}

export function folderIcon(open) {
	return icon(open ? "folder-opened" : "folder", "ft-folder");
}

// The codicon spec ("$(name)") of a file, for tab titles.
export function fileIconSpec(name) {
	const found = BY_EXTENSION.get(extensionOf(name));
	return "$(" + (found ? found[1] : "file") + ")";
}
