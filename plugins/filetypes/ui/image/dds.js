// dds.js - a DirectDraw Surface (.dds): its header (legacy or DX10), and any of its pictures - an array element or cube
// face, a mip level, a volume's slice - read into RGBA8, or RGBA floats for a float format. Block formats BC1-BC7 and
// the common uncompressed ones; an sRGB format's values show as stored.
//     const texture = readDds(bytes);   // {width, height, depth, levels, layers, format, hdr, ...}
//     const picture = texture.image(layer, level, slice);   // {width, height, data}

import { BLOCK_BYTES, decodeBlocks } from "./bc.js";
import { decodeBc7Block } from "./bc7.js";
import { decodeBc6hBlock } from "./bc6h.js";
import { halfToFloat, r11g11b10, r9g9b9e5 } from "./floats.js";

const MAGIC = 0x20534444;   // "DDS "
const HEADER = 128;
const DX10_HEADER = 20;
const CUBE_FACES = ["+X", "-X", "+Y", "-Y", "+Z", "-Z"];

// Uncompressed formats: bits a pixel, whether floats, and the writer of one pixel (view, at, out, o).
const u16 = (view, at) => view.getUint16(at, true) >> 8;
const f16 = (view, at) => halfToFloat(view.getUint16(at, true));
const f32 = (view, at) => view.getFloat32(at, true);

function plain(name, bits, hdr, write) {
	return { name, bits, hdr, write };
}

const gray8 = (view, at, out, o) => { out[o] = out[o + 1] = out[o + 2] = view.getUint8(at); out[o + 3] = 255; };

const DXGI = {
	2: plain("R32G32B32A32_FLOAT", 128, true, (v, at, out, o) => { out[o] = f32(v, at); out[o + 1] = f32(v, at + 4); out[o + 2] = f32(v, at + 8); out[o + 3] = f32(v, at + 12); }),
	6: plain("R32G32B32_FLOAT", 96, true, (v, at, out, o) => { out[o] = f32(v, at); out[o + 1] = f32(v, at + 4); out[o + 2] = f32(v, at + 8); out[o + 3] = 1; }),
	10: plain("R16G16B16A16_FLOAT", 64, true, (v, at, out, o) => { out[o] = f16(v, at); out[o + 1] = f16(v, at + 2); out[o + 2] = f16(v, at + 4); out[o + 3] = f16(v, at + 6); }),
	11: plain("R16G16B16A16_UNORM", 64, false, (v, at, out, o) => { out[o] = u16(v, at); out[o + 1] = u16(v, at + 2); out[o + 2] = u16(v, at + 4); out[o + 3] = u16(v, at + 6); }),
	16: plain("R32G32_FLOAT", 64, true, (v, at, out, o) => { out[o] = f32(v, at); out[o + 1] = f32(v, at + 4); out[o + 2] = 0; out[o + 3] = 1; }),
	24: plain("R10G10B10A2_UNORM", 32, false, (v, at, out, o) => {
		const p = v.getUint32(at, true);
		out[o] = (p & 1023) >> 2; out[o + 1] = ((p >>> 10) & 1023) >> 2; out[o + 2] = ((p >>> 20) & 1023) >> 2; out[o + 3] = ((p >>> 30) & 3) * 85;
	}),
	26: plain("R11G11B10_FLOAT", 32, true, (v, at, out, o) => { out.set(r11g11b10(v.getUint32(at, true)), o); out[o + 3] = 1; }),
	28: plain("R8G8B8A8_UNORM", 32, false, (v, at, out, o) => { out[o] = v.getUint8(at); out[o + 1] = v.getUint8(at + 1); out[o + 2] = v.getUint8(at + 2); out[o + 3] = v.getUint8(at + 3); }),
	34: plain("R16G16_FLOAT", 32, true, (v, at, out, o) => { out[o] = f16(v, at); out[o + 1] = f16(v, at + 2); out[o + 2] = 0; out[o + 3] = 1; }),
	35: plain("R16G16_UNORM", 32, false, (v, at, out, o) => { out[o] = u16(v, at); out[o + 1] = u16(v, at + 2); out[o + 2] = 0; out[o + 3] = 255; }),
	41: plain("R32_FLOAT", 32, true, (v, at, out, o) => { out[o] = out[o + 1] = out[o + 2] = f32(v, at); out[o + 3] = 1; }),
	49: plain("R8G8_UNORM", 16, false, (v, at, out, o) => { out[o] = v.getUint8(at); out[o + 1] = v.getUint8(at + 1); out[o + 2] = 0; out[o + 3] = 255; }),
	54: plain("R16_FLOAT", 16, true, (v, at, out, o) => { out[o] = out[o + 1] = out[o + 2] = f16(v, at); out[o + 3] = 1; }),
	56: plain("R16_UNORM", 16, false, (v, at, out, o) => { out[o] = out[o + 1] = out[o + 2] = u16(v, at); out[o + 3] = 255; }),
	61: plain("R8_UNORM", 8, false, gray8),
	65: plain("A8_UNORM", 8, false, gray8),
	67: plain("R9G9B9E5_SHAREDEXP", 32, true, (v, at, out, o) => { out.set(r9g9b9e5(v.getUint32(at, true)), o); out[o + 3] = 1; }),
	85: plain("B5G6R5_UNORM", 16, false, (v, at, out, o) => {
		const p = v.getUint16(at, true);
		out[o] = ((p >> 11) << 3) | (p >> 13); out[o + 1] = (((p >> 5) & 63) << 2) | ((p >> 9) & 3); out[o + 2] = ((p & 31) << 3) | ((p >> 2) & 7); out[o + 3] = 255;
	}),
	86: plain("B5G5R5A1_UNORM", 16, false, (v, at, out, o) => {
		const p = v.getUint16(at, true);
		const five = (x) => (x << 3) | (x >> 2);
		out[o] = five((p >> 10) & 31); out[o + 1] = five((p >> 5) & 31); out[o + 2] = five(p & 31); out[o + 3] = p & 0x8000 ? 255 : 0;
	}),
	87: plain("B8G8R8A8_UNORM", 32, false, (v, at, out, o) => { out[o] = v.getUint8(at + 2); out[o + 1] = v.getUint8(at + 1); out[o + 2] = v.getUint8(at); out[o + 3] = v.getUint8(at + 3); }),
	88: plain("B8G8R8X8_UNORM", 32, false, (v, at, out, o) => { out[o] = v.getUint8(at + 2); out[o + 1] = v.getUint8(at + 1); out[o + 2] = v.getUint8(at); out[o + 3] = 255; }),
	115: plain("B4G4R4A4_UNORM", 16, false, (v, at, out, o) => {
		const p = v.getUint16(at, true);
		out[o] = ((p >> 8) & 15) * 17; out[o + 1] = ((p >> 4) & 15) * 17; out[o + 2] = (p & 15) * 17; out[o + 3] = (p >> 12) * 17;
	}),
};
// Other names of the same layouts: typeless, sRGB and integer variants.
const SAME = { 1: 2, 27: 28, 29: 28, 30: 28, 25: 24, 23: 24, 90: 87, 91: 87, 92: 88, 93: 88, 62: 61, 33: 34 };

const BLOCKS = {
	70: ["bc1", "BC1_TYPELESS"], 71: ["bc1", "BC1_UNORM"], 72: ["bc1", "BC1_UNORM_SRGB"],
	73: ["bc2", "BC2_TYPELESS"], 74: ["bc2", "BC2_UNORM"], 75: ["bc2", "BC2_UNORM_SRGB"],
	76: ["bc3", "BC3_TYPELESS"], 77: ["bc3", "BC3_UNORM"], 78: ["bc3", "BC3_UNORM_SRGB"],
	79: ["bc4", "BC4_TYPELESS"], 80: ["bc4", "BC4_UNORM"], 81: ["bc4s", "BC4_SNORM"],
	82: ["bc5", "BC5_TYPELESS"], 83: ["bc5", "BC5_UNORM"], 84: ["bc5s", "BC5_SNORM"],
	94: ["bc6h", "BC6H_TYPELESS"], 95: ["bc6h", "BC6H_UF16"], 96: ["bc6hs", "BC6H_SF16"],
	97: ["bc7", "BC7_TYPELESS"], 98: ["bc7", "BC7_UNORM"], 99: ["bc7", "BC7_UNORM_SRGB"],
};

const FOURCC = {
	DXT1: ["bc1", "BC1 (DXT1)"], DXT2: ["bc2", "BC2 (DXT2)"], DXT3: ["bc2", "BC2 (DXT3)"], DXT4: ["bc3", "BC3 (DXT4)"], DXT5: ["bc3", "BC3 (DXT5)"],
	ATI1: ["bc4", "BC4 (ATI1)"], BC4U: ["bc4", "BC4_UNORM"], BC4S: ["bc4s", "BC4_SNORM"],
	ATI2: ["bc5", "BC5 (ATI2)"], BC5U: ["bc5", "BC5_UNORM"], BC5S: ["bc5s", "BC5_SNORM"],
};
// Direct3D 9's numbered float formats, as the DXGI formats they are.
const D3DFMT = { 36: 11, 111: 54, 112: 34, 113: 10, 114: 41, 115: 16, 116: 2 };

function fail(message) {
	throw new Error("Not a DDS texture this viewer reads: " + message + ".");
}

function formatOfDxgi(number) {
	if (BLOCKS[number]) return { block: BLOCKS[number][0], name: BLOCKS[number][1] };
	const known = DXGI[number] || DXGI[SAME[number]];
	if (!known) fail("DXGI format " + number);
	return { plain: known, name: DXGI[number] ? known.name : known.name + " (DXGI " + number + ")" };
}

const bitCount = (mask) => {
	let n = 0;
	for (let m = mask >>> 0; m; m >>>= 1) n += m & 1;
	return n;
};
const shiftOf = (mask) => {
	let s = 0;
	if (!mask) return 0;
	while (!((mask >>> s) & 1)) s++;
	return s;
};

// A legacy pixel format given by its masks (RGB, luminance, alpha).
function maskFormat(view, flags) {
	const bits = view.getUint32(88, true);
	const masks = [92, 96, 100, 104].map((at) => view.getUint32(at, true));
	const luminance = (flags & 0x20000) !== 0;
	const alphaOnly = (flags & 0x2) !== 0 && !(flags & 0x40) && !luminance;
	if (!(flags & 0x1)) masks[3] = alphaOnly ? masks[3] : 0;
	if (![8, 16, 24, 32].includes(bits)) fail(bits + "-bit pixels");
	const read = bits === 8 ? (v, at) => v.getUint8(at) : bits === 16 ? (v, at) => v.getUint16(at, true)
		: bits === 24 ? (v, at) => v.getUint8(at) | (v.getUint8(at + 1) << 8) | (v.getUint8(at + 2) << 16) : (v, at) => v.getUint32(at, true);
	const channel = (p, mask) => {
		const n = bitCount(mask);
		return n ? Math.round((((p & mask) >>> shiftOf(mask)) * 255) / (Math.pow(2, n) - 1)) : 0;
	};
	const write = (v, at, out, o) => {
		const p = read(v, at) >>> 0;
		if (alphaOnly) out[o] = out[o + 1] = out[o + 2] = channel(p, masks[3]);
		else if (luminance) out[o] = out[o + 1] = out[o + 2] = channel(p, masks[0]);
		else { out[o] = channel(p, masks[0]); out[o + 1] = channel(p, masks[1]); out[o + 2] = channel(p, masks[2]); }
		out[o + 3] = masks[3] && !alphaOnly ? channel(p, masks[3]) : 255;
	};
	const kind = alphaOnly ? "A" : luminance ? (masks[3] ? "L" + bitCount(masks[0]) + "A" + bitCount(masks[3]) : "L" + bitCount(masks[0])) : (masks[3] ? "RGBA" : "RGB");
	return { plain: { bits, hdr: false, write }, name: kind + " " + bits + "-bit" };
}

function legacyFormat(view) {
	const flags = view.getUint32(80, true);
	if (!(flags & 0x4)) return maskFormat(view, flags);
	const code = view.getUint32(84, true);
	const text = String.fromCharCode(code & 255, (code >> 8) & 255, (code >> 16) & 255, code >>> 24);
	if (FOURCC[text]) return { block: FOURCC[text][0], name: FOURCC[text][1] };
	if (D3DFMT[code]) return formatOfDxgi(D3DFMT[code]);
	return fail("the FourCC '" + text.replace(/[^ -~]/g, "?") + "'");
}

export function readDds(bytes) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	if (bytes.length < HEADER || view.getUint32(0, true) !== MAGIC || view.getUint32(4, true) !== 124) fail("no DDS header");
	const flags = view.getUint32(8, true);
	const height = view.getUint32(12, true);
	const width = view.getUint32(16, true);
	const caps2 = view.getUint32(112, true);
	const levels = Math.max(1, flags & 0x20000 ? view.getUint32(28, true) : 1);
	const dx10 = view.getUint32(84, true) === 0x30315844 && (view.getUint32(80, true) & 0x4) !== 0;
	if (dx10 && bytes.length < HEADER + DX10_HEADER) fail("no DX10 header");
	const format = dx10 ? formatOfDxgi(view.getUint32(HEADER, true)) : legacyFormat(view);
	const dimension = dx10 ? view.getUint32(HEADER + 4, true) : 0;
	const cube = dx10 ? (view.getUint32(HEADER + 8, true) & 4) !== 0 : (caps2 & 0x200) !== 0;
	const volume = dx10 ? dimension === 4 : (caps2 & 0x200000) !== 0;
	const depth = volume ? Math.max(1, view.getUint32(24, true)) : 1;
	const elements = dx10 ? Math.max(1, view.getUint32(HEADER + 12, true)) : 1;
	const faces = cube ? (dx10 ? 6 : CUBE_FACES.filter((_, i) => caps2 & (0x400 << i)).length || 6) : 1;
	if (!width || !height) fail("no size");
	const texture = {
		width, height, depth, levels, layers: elements * faces, cube, format: format.name,
		hdr: format.block ? format.block.startsWith("bc6h") : format.plain.hdr,
		start: HEADER + (dx10 ? DX10_HEADER : 0),
		layerName: (layer) => (cube ? (elements > 1 ? "Cube " + Math.floor(layer / 6) + " " : "") + CUBE_FACES[layer % 6] : "Element " + layer),
	};
	const levelSize = (level) => sizeOf(format, Math.max(1, width >> level), Math.max(1, height >> level));
	const levelBytes = (level) => levelSize(level) * Math.max(1, depth >> level);
	let layerBytes = 0;
	for (let l = 0; l < levels; l++) layerBytes += levelBytes(l);
	texture.image = (layer, level, slice) => {
		let at = texture.start + layer * layerBytes;
		for (let l = 0; l < level; l++) at += levelBytes(l);
		at += (slice || 0) * levelSize(level);
		const w = Math.max(1, width >> level);
		const h = Math.max(1, height >> level);
		if (at + levelSize(level) > bytes.length) fail("its data ends before element " + layer + ", mip " + level);
		return { width: w, height: h, data: pixelsOf(format, bytes, view, at, w, h), hdr: texture.hdr };
	};
	return texture;
}

function sizeOf(format, w, h) {
	if (format.block) return Math.max(1, (w + 3) >> 2) * Math.max(1, (h + 3) >> 2) * BLOCK_BYTES[format.block];
	return Math.ceil((w * format.plain.bits) / 8) * h;
}

function pixelsOf(format, bytes, view, at, w, h) {
	if (format.block === "bc7") return decodeBlocks(bytes, at, w, h, "bc7", decodeBc7Block);
	if (format.block === "bc6h" || format.block === "bc6hs") {
		const signed = format.block === "bc6hs";
		return decodeBlocks(bytes, at, w, h, format.block, (b, a, texels) => decodeBc6hBlock(b, a, texels, signed), Float32Array);
	}
	if (format.block) return decodeBlocks(bytes, at, w, h, format.block);
	const { bits, hdr, write } = format.plain;
	const out = hdr ? new Float32Array(w * h * 4) : new Uint8Array(w * h * 4);
	const pitch = Math.ceil((w * bits) / 8);
	const step = bits / 8;
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++) write(view, at + y * pitch + x * step, out, (y * w + x) * 4);
	return out;
}
