// readout.js - how the image viewer writes what it shows: the texel under the pointer (bytes with their hex, floats with
// four decimals), the picture's description, and theme colors as the numbers WebGL takes.
//     texelText({ x: 3, y: 4 }, [255, 128, 0, 255], false) -> "X 3  Y 4   R 255  G 128  B 0  A 255   #FF8000FF"

import { formatSize } from "../chrome.js";

// `value` RGBA bytes, or floats when `float`.
export function texelText(at, value, float) {
	const place = "X " + at.x + "  Y " + at.y;
	if (!value) return place;
	const channels = ["R", "G", "B", "A"].map((c, i) => c + " " + (float ? Number(value[i]).toFixed(4) : value[i])).join("  ");
	if (float) return place + "   " + channels;
	const hex = value.map((v) => v.toString(16).padStart(2, "0")).join("").toUpperCase();
	return place + "   " + channels + "   #" + hex;
}

// "1024 x 1024", the format, a texture's mips, faces or elements and depth, the file's size.
export function describe(shown, texture, bytes) {
	const parts = [shown.width + " x " + shown.height, shown.format];
	if (texture) {
		if (texture.levels > 1) parts.push(texture.levels + " mips");
		if (texture.layers > 1) parts.push(texture.cube ? (texture.layers === 6 ? "cube" : texture.layers / 6 + " cubes") : texture.layers + " elements");
		if (texture.depth > 1) parts.push("depth " + texture.depth);
	}
	if (bytes >= 0) parts.push(formatSize(bytes));
	return parts.filter(Boolean);
}

let probe = null;

// A CSS color (a theme token's value) as [r, g, b], each 0..1.
export function rgbOf(cssColor) {
	if (!probe) probe = document.createElement("canvas").getContext("2d");
	probe.fillStyle = "transparent";
	probe.fillStyle = cssColor || "transparent";
	const value = probe.fillStyle;
	if (value.startsWith("#")) return [1, 3, 5].map((i) => parseInt(value.slice(i, i + 2), 16) / 255);
	const parts = value.match(/[\d.]+/g) || [0, 0, 0];
	return parts.slice(0, 3).map((v) => Number(v) / 255);
}
