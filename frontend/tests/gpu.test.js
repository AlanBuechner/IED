// gpu.test.js - the GPU debugger's buffer layouts (plugins/gpu/ui/layout.js): the layout text as fields at their
// offsets, what it refuses, and bytes read as rows of a layout or as hex.

import { test, assertEqual, assertTrue, assertThrows } from "./harness.js";
import { parseLayout, readRows, half } from "/plugins/ied.gpu/ui/layout.js";

const shape = (layout) => layout.fields.map((f) => [f.name, f.offset, f.components, f.bytes]);

test("gpu layout: fields pack in order, pad skips bytes", () => {
	const layout = parseLayout("float3 position; pad 4; float2 uv\nunorm8x4 color");
	assertEqual(shape(layout), [["position", 0, 3, 12], ["uv", 16, 2, 8], ["color", 24, 4, 4]]);
	assertEqual(layout.stride, 28);
});

test("gpu layout: sized bases, matrices, arrays and unnamed fields", () => {
	assertEqual(shape(parseLayout("uint16x2 pair, float4x4 world")), [["pair", 0, 2, 4], ["world", 4, 16, 64]]);
	assertEqual(shape(parseLayout("int index[3]")), [["index[0]", 0, 1, 4], ["index[1]", 4, 1, 4], ["index[2]", 8, 1, 4]]);
	assertEqual(shape(parseLayout("half2")), [["half2", 0, 2, 4]]);
	assertEqual(parseLayout("float4x3 basis").fields.map((f) => [f.width, f.components]), [[4, 12]], "a matrix is its vectors as stored");
	assertTrue(parseLayout(" HEX ").hex, "hex in any case");
});

test("gpu layout: what it cannot read is named", () => {
	assertTrue(assertThrows(() => parseLayout("vec3 position"), Error).message.includes("vec3 position"));
	assertThrows(() => parseLayout("float5 x"), Error);
	assertThrows(() => parseLayout("floatx y"), Error);
	assertThrows(() => parseLayout("float x[0]"), Error);
	assertThrows(() => parseLayout(""), Error, "an empty layout");
	assertThrows(() => parseLayout("pad 4"), Error, "padding alone");
});

test("gpu layout: rows read each component little-endian, normalized and half values scaled", () => {
	const bytes = new ArrayBuffer(16);
	const view = new DataView(bytes);
	view.setFloat32(0, 1.5, true);
	view.setUint16(4, 0x3c00, true);   // half 1.0
	view.setUint8(6, 255);
	view.setInt8(7, -127);
	view.setUint32(8, 7, true);
	view.setFloat32(12, -2, true);
	const rows = readRows(parseLayout("float a; half b; unorm8 c; snorm8 d; uint e"), bytes);
	assertEqual(rows.map((r) => r.cells), [["1.5", "1", "1", "-1", "7"]], "the last 4 bytes are not a whole element");
	assertEqual(half(0xfc00), -Infinity);
	assertTrue(Number.isNaN(half(0x7e00)));
});

test("gpu layout: hex rows are 16 bytes with their text", () => {
	const bytes = new TextEncoder().encode("Flashbang buffer view!").buffer;
	const rows = readRows(parseLayout("hex"), bytes);
	assertEqual(rows.length, 2);
	assertEqual(rows[0].cells[0], "46");
	assertEqual(rows[0].text, "Flashbang buffer");
	assertEqual(rows[1].text, " view!");
});
