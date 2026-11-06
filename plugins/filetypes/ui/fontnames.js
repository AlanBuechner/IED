// fontnames.js - what a font file says it is: its family, style, full name, version, copyright and glyph count, read
// from the OpenType 'name' and 'maxp' tables of a TTF, OTF, TrueType collection (its first font) or WOFF (its tables
// inflated). WOFF2's are Brotli-coded: only its kind is known.
//     const names = await fontNames(bytes);   // {family, style, full, version, copyright, glyphs, kind}

const tag = (bytes, at) => String.fromCharCode(bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]);

async function inflate(bytes) {
	const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate"));
	return new Uint8Array(await new Response(stream).arrayBuffer());
}

// {tag: Uint8Array} of the tables `wanted`.
async function tablesOf(bytes, wanted) {
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const tables = {};
	const kind = tag(bytes, 0);
	if (kind === "wOFF") {
		const count = view.getUint16(12);
		for (let i = 0; i < count; i++) {
			const at = 44 + i * 20;
			const name = tag(bytes, at);
			if (!wanted.includes(name)) continue;
			const offset = view.getUint32(at + 4);
			const compressed = view.getUint32(at + 8);
			const length = view.getUint32(at + 12);
			const data = bytes.subarray(offset, offset + compressed);
			tables[name] = compressed < length ? await inflate(data) : data;
		}
		return tables;
	}
	const start = kind === "ttcf" ? view.getUint32(12) : 0;
	const count = view.getUint16(start + 4);
	for (let i = 0; i < count; i++) {
		const at = start + 12 + i * 16;
		const name = tag(bytes, at);
		if (wanted.includes(name)) tables[name] = bytes.subarray(view.getUint32(at + 8), view.getUint32(at + 8) + view.getUint32(at + 12));
	}
	return tables;
}

// The name records' strings by name id: Windows Unicode (US English first), else Macintosh Roman.
export function readNames(table) {
	const view = new DataView(table.buffer, table.byteOffset, table.byteLength);
	const count = view.getUint16(2);
	const strings = view.getUint16(4);
	const found = {};
	const rank = {};
	for (let i = 0; i < count; i++) {
		const at = 6 + i * 12;
		const platform = view.getUint16(at);
		const encoding = view.getUint16(at + 2);
		const language = view.getUint16(at + 4);
		const id = view.getUint16(at + 6);
		const length = view.getUint16(at + 8);
		const offset = strings + view.getUint16(at + 10);
		const windows = platform === 3 && (encoding === 1 || encoding === 10);
		const mac = platform === 1 && encoding === 0;
		if (!windows && !mac) continue;
		const score = windows ? (language === 0x409 ? 3 : 2) : 1;
		if ((rank[id] || 0) >= score) continue;
		let text = "";
		if (windows) for (let k = 0; k + 1 < length; k += 2) text += String.fromCharCode(view.getUint16(offset + k));
		else for (let k = 0; k < length; k++) text += String.fromCharCode(table[offset + k]);
		found[id] = text;
		rank[id] = score;
	}
	return found;
}

export async function fontNames(bytes) {
	const kind = tag(bytes, 0);
	const label = { wOFF: "WOFF", wOF2: "WOFF2", OTTO: "OpenType (CFF)", ttcf: "TrueType collection", true: "TrueType" }[kind] || (bytes[0] === 0 && bytes[1] === 1 ? "TrueType" : "");
	if (kind === "wOF2") return { kind: label };
	try {
		const tables = await tablesOf(bytes, ["name", "maxp"]);
		const names = tables.name ? readNames(tables.name) : {};
		const glyphs = tables.maxp && tables.maxp.length >= 6 ? (tables.maxp[4] << 8) | tables.maxp[5] : 0;
		return { kind: label, family: names[16] || names[1], style: names[17] || names[2], full: names[4], version: names[5], copyright: names[0], glyphs };
	}
	catch (e) {
		return { kind: label };
	}
}
