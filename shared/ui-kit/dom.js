// dom.js - building elements for the shell and plugin UI modules. Theme tokens only (frontend/css/theme.css).
//     import { h, icon } from "/ui-kit/dom.js";
//     h("div.row.sel", { on: { click: open }, title: path }, icon("file"), h("span.name", null, name));

import { glyph, isGlyph, tokenColor } from "./fileglyphs.js";

// An element: `tag` is "name.class1.class2"; `props` sets attributes, with `on` for listeners, `style` for properties
// (custom ones included), `dataset`, `text`; children are nodes, strings, or arrays of them (null skipped).
export function h(tag, props, ...children) {
	const [name, ...classes] = tag.split(".");
	const el = document.createElement(name || "div");
	if (classes.length) el.className = classes.join(" ");
	if (props) applyProps(el, props);
	append(el, children);
	return el;
}

function applyProps(el, props) {
	for (const [key, value] of Object.entries(props)) {
		if (value === undefined || value === null || value === false) continue;
		if (key === "on") for (const [type, fn] of Object.entries(value)) el.addEventListener(type, fn);
		else if (key === "style") for (const [prop, v] of Object.entries(value)) el.style.setProperty(prop, v);
		else if (key === "dataset") Object.assign(el.dataset, value);
		else if (key === "text") el.textContent = value;
		else if (key === "class") el.className = [el.className, value].filter(Boolean).join(" ");
		else el.setAttribute(key, value === true ? "" : value);
	}
}

export function append(el, children) {
	for (const child of children.flat(Infinity)) {
		if (child === null || child === undefined || child === false) continue;
		el.append(child instanceof Node ? child : String(child));
	}
	return el;
}

export function clear(el) {
	el.replaceChildren();
	return el;
}

// `el`'s children replaced by `children` (nodes, strings, arrays; null skipped, unlike Element.replaceChildren).
export function replace(el, ...children) {
	el.replaceChildren();
	return append(el, children);
}

// A codicon, painted in the current text color: `name` as codicons names it ("debug-start").
export function icon(name, extraClass) {
	const el = document.createElement("span");
	el.className = extraClass ? "ci " + extraClass : "ci";
	el.style.setProperty("--ci", "url(/codicons/" + encodeURIComponent(name) + ".svg)");
	el.setAttribute("aria-hidden", "true");
	return el;
}

// An icon as a manifest gives it: "$(name)" is a codicon ("$(name~spin)" turning), anything else an SVG path relative to
// `base` (a plugin's folder URL); or a file icon's spec (fileglyphs.js): a drawn shape, or {icon, base, color} for a
// plugin's own picture in a theme color. Null when there is none.
export function iconFrom(spec, base, extraClass) {
	if (!spec) return null;
	if (typeof spec === "object") return isGlyph(spec) ? glyph(spec, extraClass) : tinted(iconFrom(spec.icon, spec.base || base, extraClass), spec.color);
	const codicon = /^\$\(([a-z0-9-]+)(~spin)?\)$/.exec(spec);
	if (codicon) return icon(codicon[1], codicon[2] ? (extraClass ? extraClass + " spin" : "spin") : extraClass);
	const el = document.createElement("span");
	el.className = extraClass ? "ci " + extraClass : "ci";
	el.style.setProperty("--ci", "url(" + JSON.stringify(new URL(spec, base || location.href).href) + ")");
	el.setAttribute("aria-hidden", "true");
	return el;
}

function tinted(el, color) {
	if (el && color) el.style.color = tokenColor(color, "");
	return el;
}

// Whether a text field (or anything editable) has the focus: keys without a modifier belong to it.
export function isTextInput(el) {
	if (!el) return false;
	if (el.isContentEditable) return true;
	if (el.tagName === "TEXTAREA" || el.tagName === "SELECT") return true;
	if (el.tagName !== "INPUT") return false;
	return !["button", "checkbox", "radio", "range", "color", "submit", "reset"].includes(el.type);
}
