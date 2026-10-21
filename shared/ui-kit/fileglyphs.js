// fileglyphs.js - the icons of files, folders, projects and solutions (docs/15 "Icons"): one simple glyph each, as
// Visual Studio and VS Code draw them - a file is its type's letters in its hue ("C4", "h", "{}"), a project its
// language's letters in a thin tile, a solution two tiles, a folder the tan folder. Which file or project gets which
// letters and hue is the plugins' to say (`fileIcons`, `projectIcons`, docs/02); this module knows no language.
//     glyph(fileIconSpec("main.cpp", contributions))

const SVG = "http://www.w3.org/2000/svg";

export const FOLDER = { shape: "folder" };
export const FOLDER_OPEN = { shape: "folder-open" };
export const SOLUTION = { shape: "solution" };
export const PROJECT = { shape: "project" };
export const FILE = { shape: "file" };

const SHAPES = new Set(["file", "folder", "folder-open", "project", "solution"]);
const FILE_LABEL = { width: 15, most: 12.5 };
const TILE_LABEL = { width: 11, most: 9 };

function node(name, attributes, parent) {
	const el = document.createElementNS(SVG, name);
	for (const [key, value] of Object.entries(attributes)) el.setAttribute(key, String(value));
	if (parent) parent.append(el);
	return el;
}

// A token name as a CSS value: "--hue-blue" -> "var(--hue-blue)"; anything else is not taken.
export function tokenColor(token, fallback) {
	return typeof token === "string" && /^--[a-z0-9-]+$/.test(token) ? "var(" + token + ")" : fallback;
}

// Whether a spec is drawn here (a shape or a label) rather than a plugin's own picture (`icon`).
export function isGlyph(spec) {
	return Boolean(spec && typeof spec === "object" && (SHAPES.has(spec.shape) || spec.label));
}

// A label's width in ems, near enough for a bold UI font: wide capitals, narrow brackets.
function ems(text) {
	let width = 0;
	for (const c of text) width += /[MW]/.test(c) ? 0.9 : /[A-Z#+<>$]/.test(c) ? 0.66 : /[{}()[\]|.:]/.test(c) ? 0.38 : 0.56;
	return width;
}

// The font size that fits `text` across `box.width` units, at most `box.most`.
export function labelSize(text, box) {
	return Math.min(box.most, box.width / Math.max(ems(text), 0.5));
}

function label(svg, text, box) {
	const size = labelSize(text, box);
	const t = node("text", { x: 8, y: 8 + size * 0.36, "text-anchor": "middle", "font-size": size.toFixed(2), class: "fg-label" }, svg);
	t.textContent = text;
}

// A file: its label; without one, a page with its corner folded.
function file(svg, spec) {
	if (spec.label) return label(svg, spec.label, FILE_LABEL);
	node("path", { d: "M3.5 1.5h6l3.5 3.5v9.5h-9.5z M9.5 1.5v3.5h3.5", class: "fg-page" }, svg);
}

function folder(svg, open) {
	if (!open) {
		node("path", { d: "M0.5 2.5h5l1.5 1.5h8.5v10.5h-15z", class: "fg-folder" }, svg);
		return;
	}
	node("path", { d: "M0.5 2.5h5l1.5 1.5h7.5v2.5h-10.5l-3.5 8z", class: "fg-folder-back" }, svg);
	node("path", { d: "M4 7.5h11.5l-3 7h-12z", class: "fg-folder" }, svg);
}

function project(svg, spec) {
	node("rect", { x: 1, y: 1.5, width: 14, height: 13, rx: 2.5, class: "fg-tile" }, svg);
	if (spec.label) label(svg, spec.label, TILE_LABEL);
}

// Two tiles, the one behind showing only its edge.
function solution(svg) {
	node("path", { d: "M5.5 4.5v-1.5a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2h-1.5", class: "fg-tile-back" }, svg);
	node("rect", { x: 1, y: 5, width: 10.5, height: 10.5, rx: 2, class: "fg-tile" }, svg);
}

const DRAW = { file, folder: (svg) => folder(svg, false), "folder-open": (svg) => folder(svg, true), project, solution };
const HUES = { file: "var(--icon-line)", project: "var(--icon-project)", solution: "var(--icon-solution)" };

// The glyph a spec describes: {shape: file|folder|folder-open|project|solution, label?, color? (a theme token's name)}.
// `extraClass` sizes it as it would a codicon ("sm", "lg").
export function glyph(spec, extraClass) {
	const s = spec || FILE;
	const shape = SHAPES.has(s.shape) ? s.shape : "file";
	const svg = node("svg", { viewBox: "0 0 16 16", class: "ci fg fg-" + shape + (extraClass ? " " + extraClass : ""), "aria-hidden": "true" });
	if (HUES[shape]) svg.style.setProperty("--fg-hue", shape === "solution" ? HUES.solution : tokenColor(s.color, HUES[shape]));
	DRAW[shape](svg, s);
	return svg;
}

function lower(text) {
	return String(text || "").toLowerCase();
}

function baseName(path) {
	const text = String(path || "");
	return text.slice(Math.max(text.lastIndexOf("/"), text.lastIndexOf("\\")) + 1);
}

// The extension of `extensions` that `file` ends with, longest first (".d.ts" over ".ts"); "" for none.
function matchedExtension(file, extensions) {
	let best = "";
	for (const extension of extensions || []) {
		const e = lower(extension).startsWith(".") ? lower(extension) : "." + lower(extension);
		if (file.endsWith(e) && file.length > e.length && e.length > best.length) best = e;
	}
	return best;
}

// The spec of the file `name` from the plugins' fileIcons ([{plugin: {base}, value}]): a rule naming the file wins
// over one naming its extension, and a longer extension over a shorter; on a tie the later plugin's.
export function fileIconSpec(name, contributions) {
	const file = lower(baseName(name));
	let best = null;
	let score = 0;
	for (const { plugin, value } of contributions || []) {
		const named = (value.fileNames || []).some((n) => lower(n) === file);
		const s = named ? 1000 : matchedExtension(file, value.extensions).length;
		if (s > 0 && s >= score) {
			score = s;
			best = { plugin, value };
		}
	}
	return best ? specOf(best.value, best.plugin, "file") : FILE;
}

function specOf(value, plugin, shape) {
	const spec = { label: value.label, color: value.color, icon: value.icon, base: plugin && plugin.base };
	if (value.label || !value.icon) spec.shape = shape;
	return spec;
}

// The spec of a project (a solution's project: {data: its .iedproj}) from the plugins' projectIcons: the one for the
// language its file names, else one whose presets or build system it uses, else one whose marker file is among
// `rootNames` (the names in its root folder, when known).
export function projectIconSpec(project, contributions, rootNames) {
	const data = (project && project.data) || {};
	const entries = contributions || [];
	const presets = data.presets || [];
	const system = data.buildSystem && data.buildSystem.id;
	const names = (rootNames || []).map(lower);
	const marked = (marker) => (marker.startsWith("*.") ? names.some((n) => n.endsWith(lower(marker.slice(1)))) : names.includes(lower(marker)));
	const found = (test) => entries.find(({ value }) => test(value));
	const hit = (data.language && found((v) => v.languageId === data.language))
		|| found((v) => (v.presets || []).some((p) => presets.includes(p)) || Boolean(system && (v.buildSystems || []).includes(system)))
		|| found((v) => (v.markers || []).some(marked));
	return hit ? specOf(hit.value, hit.plugin, "project") : PROJECT;
}

// Whether a project's icon could still change with its root folder's names (no language, preset or build system
// settles it, and some plugin names marker files).
export function projectNeedsMarkers(project, contributions) {
	const settled = projectIconSpec(project, contributions, []);
	return settled === PROJECT && (contributions || []).some(({ value }) => (value.markers || []).length > 0);
}
