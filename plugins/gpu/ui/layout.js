// layout.js - the buffer view's layouts (docs/22 "Buffer view"): "float3 position; pad 4; float2 uv" read as one
// element's fields, tightly packed in order, and bytes read as rows of those fields' values. "hex" reads 16 bytes a row.
// Pure: no DOM.

import { value } from "./format.js";

// name: [bytes, how one component reads]
const BASES = {
	float: [4, "f32"], half: [2, "f16"], double: [8, "f64"], int: [4, "i32"], uint: [4, "u32"], bool: [4, "u32"],
	int8: [1, "i8"], uint8: [1, "u8"], int16: [2, "i16"], uint16: [2, "u16"], int64: [8, "i64"], uint64: [8, "u64"],
	unorm8: [1, "un8"], snorm8: [1, "sn8"], unorm16: [2, "un16"], snorm16: [2, "sn16"],
	byte: [1, "u8"], short: [2, "i16"], ushort: [2, "u16"], dword: [4, "u32"],
};
const NAMES = Object.keys(BASES).sort((a, b) => b.length - a.length);

// A type token: a base, then a vector size (float3, uint16x2) or a matrix's vector size x vectors as stored (float4x3:
// 3 vectors of 4). `width` is the vector size.
function typeOf(token) {
	const base = NAMES.find((name) => token.startsWith(name));
	if (!base) return null;
	const shape = /^(?:x?([1-4])(?:x([1-4]))?)?$/.exec(token.slice(base.length));
	if (!shape) return null;
	const width = Number(shape[1] || 1);
	const components = width * Number(shape[2] || 1);
	const [size, read] = BASES[base];
	return { base, read, size, width, components, bytes: size * components };
}

// The layout text as fields at their offsets and the element's stride; throws an Error naming what it cannot read.
export function parseLayout(text) {
	const source = (text || "").trim();
	if (/^hex$/i.test(source)) return { hex: true, fields: [], stride: 16 };
	const fields = [];
	let offset = 0;
	for (const part of source.split(/[;,\n]+/)) {
		const field = part.trim();
		if (!field) continue;
		const pad = /^pad\s+(\d+)$/i.exec(field);
		if (pad) {
			offset += Number(pad[1]);
			continue;
		}
		const m = /^([A-Za-z][A-Za-z0-9]*)(?:\s+([A-Za-z_][A-Za-z0-9_]*))?\s*(?:\[\s*(\d+)\s*\])?$/.exec(field);
		const type = m && typeOf(m[1].toLowerCase());
		if (!type) throw new Error("Cannot read \"" + field + "\": a field is a type (float3, uint16, unorm8x4, float4x4), an optional name and [count]; or pad N.");
		const count = m[3] ? Number(m[3]) : 1;
		if (count < 1 || count > 4096) throw new Error("\"" + field + "\": the count must be 1 to 4096.");
		const name = m[2] || m[1];
		for (let i = 0; i < count; i++) {
			fields.push({ name: count > 1 ? name + "[" + i + "]" : name, type: m[1], ...type, offset });
			offset += type.bytes;
		}
	}
	if (!fields.length) throw new Error("The layout has no fields.");
	return { hex: false, fields, stride: offset };
}

function component(view, at, read) {
	switch (read) {
		case "f32": return value(view.getFloat32(at, true));
		case "f64": return value(view.getFloat64(at, true));
		case "f16": return value(half(view.getUint16(at, true)));
		case "i32": return String(view.getInt32(at, true));
		case "u32": return String(view.getUint32(at, true));
		case "i8": return String(view.getInt8(at));
		case "u8": return String(view.getUint8(at));
		case "i16": return String(view.getInt16(at, true));
		case "u16": return String(view.getUint16(at, true));
		case "i64": return String(view.getBigInt64(at, true));
		case "u64": return String(view.getBigUint64(at, true));
		case "un8": return value(view.getUint8(at) / 255);
		case "sn8": return value(Math.max(-1, view.getInt8(at) / 127));
		case "un16": return value(view.getUint16(at, true) / 65535);
		case "sn16": return value(Math.max(-1, view.getInt16(at, true) / 32767));
		default: return "?";
	}
}

export function half(bits) {
	const sign = bits & 0x8000 ? -1 : 1;
	const exponent = (bits >> 10) & 0x1f;
	const fraction = bits & 0x3ff;
	if (exponent === 0) return sign * fraction * Math.pow(2, -24);
	if (exponent === 31) return fraction ? NaN : sign * Infinity;
	return sign * (1 + fraction / 1024) * Math.pow(2, exponent - 15);
}

// The rows `bytes` (an ArrayBuffer) hold: per element its cells, each field's components in order; a hex row is 16
// byte cells and the bytes as text. An element the bytes end inside is left out.
export function readRows(layout, bytes) {
	const view = new DataView(bytes);
	const rows = [];
	if (layout.hex) {
		for (let at = 0; at < view.byteLength; at += 16) {
			const cells = [];
			let text = "";
			for (let i = at; i < Math.min(at + 16, view.byteLength); i++) {
				const b = view.getUint8(i);
				cells.push(b.toString(16).padStart(2, "0"));
				text += b >= 32 && b < 127 ? String.fromCharCode(b) : ".";
			}
			rows.push({ cells, text });
		}
		return rows;
	}
	for (let at = 0; at + layout.stride <= view.byteLength; at += layout.stride) {
		const cells = [];
		for (const field of layout.fields)
			for (let c = 0; c < field.components; c++) cells.push(component(view, at + field.offset + c * field.size, field.read));
		rows.push({ cells });
	}
	return rows;
}
