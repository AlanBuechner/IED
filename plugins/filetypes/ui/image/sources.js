// sources.js - what a picture file becomes for the image viewer: a texture (DDS: its elements, faces, mips and slices),
// decoded pixels (TGA, Radiance HDR), a browser picture read exactly as stored (PNG, JPEG, WebP, AVIF, BMP, ICO), or an
// element the page draws (an SVG, an animated GIF, PNG or WebP - drawn by the browser so it keeps moving).
//     const picture = await openPicture("png", bytes);   // {kind: "bitmap", bitmap, width, height, format}

import { readDds } from "./dds.js";
import { decodeTga } from "./tga.js";
import { decodeHdr } from "./hdr.js";

const TYPES = {
	png: "image/png", apng: "image/apng", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", bmp: "image/bmp",
	ico: "image/x-icon", avif: "image/avif", svg: "image/svg+xml",
};
const NAMES = { jpg: "JPEG", jpeg: "JPEG", apng: "APNG", ico: "Icon" };

export function extensionOf(path) {
	const name = path.split("/").pop();
	const dot = name.lastIndexOf(".");
	return dot > 0 ? name.slice(dot + 1).toLowerCase() : "";
}

const ascii = (bytes, at, length) => String.fromCharCode(...bytes.subarray(at, at + length));

// Whether a GIF, PNG or WebP holds more than one frame.
export function isAnimated(ext, bytes) {
	if (ext === "gif") {
		let frames = 0;
		for (let i = 0; i + 2 < bytes.length && frames < 2; i++) if (bytes[i] === 0x21 && bytes[i + 1] === 0xf9 && bytes[i + 2] === 0x04) frames++;
		return frames > 1;
	}
	if (ext === "png" || ext === "apng") {
		for (let at = 8; at + 8 <= bytes.length;) {
			const length = ((bytes[at] << 24) | (bytes[at + 1] << 16) | (bytes[at + 2] << 8) | bytes[at + 3]) >>> 0;
			const type = ascii(bytes, at + 4, 4);
			if (type === "acTL") return true;
			if (type === "IDAT") return false;
			at += 12 + length;
		}
		return false;
	}
	if (ext === "webp") return bytes.length > 21 && ascii(bytes, 0, 4) === "RIFF" && ascii(bytes, 12, 4) === "VP8X" && (bytes[20] & 0x02) !== 0;
	return false;
}

// An SVG without a size of its own takes its viewBox's proportions.
function svgSize(text) {
	const box = /viewBox\s*=\s*["']\s*[-\d.e]+[\s,]+[-\d.e]+[\s,]+([\d.e]+)[\s,]+([\d.e]+)/i.exec(text || "");
	if (!box) return { width: 300, height: 150 };
	const [w, h] = [Number(box[1]), Number(box[2])];
	return w > 0 && h > 0 ? { width: 512, height: Math.max(1, Math.round((512 * h) / w)) } : { width: 300, height: 150 };
}

async function elementOf(bytes, type, vector, format, text) {
	const url = URL.createObjectURL(new Blob([bytes], { type }));
	const element = new Image();
	element.src = url;
	try {
		await element.decode();
	}
	catch (e) {
		URL.revokeObjectURL(url);
		throw new Error("The browser cannot show this " + format + " file" + (vector ? " (is the SVG well formed?)" : "") + ".");
	}
	let width = element.naturalWidth;
	let height = element.naturalHeight;
	if (vector && (!width || !height)) ({ width, height } = svgSize(text));
	return { kind: "element", element, width, height, vector, format, url };
}

export function openSvg(text) {
	return elementOf(new TextEncoder().encode(text), TYPES.svg, true, "SVG", text);
}

export async function openPicture(ext, bytes) {
	if (ext === "dds") return { kind: "texture", texture: readDds(bytes) };
	if (ext === "tga" || ext === "hdr") {
		const pixels = ext === "tga" ? decodeTga(bytes) : decodeHdr(bytes);
		return { kind: "pixels", pixels, format: pixels.format };
	}
	const type = TYPES[ext] || "application/octet-stream";
	const format = NAMES[ext] || ext.toUpperCase();
	if (ext === "svg") return openSvg(new TextDecoder().decode(bytes));
	if (isAnimated(ext, bytes)) return elementOf(bytes, type, false, format + ", animated");
	try {
		const bitmap = await createImageBitmap(new Blob([bytes], { type }), { premultiplyAlpha: "none", colorSpaceConversion: "none" });
		return { kind: "bitmap", bitmap, width: bitmap.width, height: bitmap.height, format };
	}
	catch (e) {
		return elementOf(bytes, type, false, format);
	}
}
