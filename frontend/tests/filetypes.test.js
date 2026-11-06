// filetypes.test.js - the File Types plugin's decoders (docs/33): TGA (raw, RLE, color-mapped, 16-bit, corners), DDS
// (BC1, masks, mips, cube faces, DX10 floats), Radiance HDR (flat and run-length scanlines), and BC7 and BC6H blocks
// against Pillow's decode of the same bytes - one seeded random block per mode (every mode of each matched Pillow on
// 512 random blocks a mode, and on the DirectX samples' BC7 and BC6H textures, when written).

import { test, assertEqual, assertTrue, assertThrows } from "./harness.js";
import { decodeTga } from "/plugins/ied.filetypes/ui/image/tga.js";
import { readDds } from "/plugins/ied.filetypes/ui/image/dds.js";
import { decodeHdr } from "/plugins/ied.filetypes/ui/image/hdr.js";
import { decodeBc7Block } from "/plugins/ied.filetypes/ui/image/bc7.js";
import { decodeBc6hBlock } from "/plugins/ied.filetypes/ui/image/bc6h.js";
import { halfToFloat, r11g11b10 } from "/plugins/ied.filetypes/ui/image/floats.js";
import { fitted, kept, rescaled, steppedZoom, texelAt, zoomedAt } from "/plugins/ied.filetypes/ui/image/viewport.js";
import { isAnimated } from "/plugins/ied.filetypes/ui/image/sources.js";
import { texelText } from "/plugins/ied.filetypes/ui/image/readout.js";
import { bytesPerRow, inspect, offsetDigits, parseOffset } from "/plugins/ied.filetypes/ui/hexrows.js";
import { readNames } from "/plugins/ied.filetypes/ui/fontnames.js";
import { totalOf } from "/plugins/ied.filetypes/ui/fileurl.js";
import { formatSize } from "/plugins/ied.filetypes/ui/chrome.js";

const hex = (text) => Uint8Array.from(text.match(/../g), (b) => parseInt(b, 16));
const list = (bytes) => Array.from(bytes).join(",");

function tga(type, width, height, depth, descriptor, body, map) {
	const header = [0, map ? 1 : 0, type, 0, 0, map ? map.length / 3 : 0, 0, map ? 24 : 0, 0, 0, 0, 0, width, 0, height, 0, depth, descriptor];
	return Uint8Array.from(header.concat(map || [], body));
}

test("filetypes: a TGA from either corner, raw and run-length coded", () => {
	// Stored bottom row first: blue, white; then red, green (BGR).
	const bottomUp = tga(2, 2, 2, 24, 0, [255, 0, 0, 255, 255, 255, 0, 0, 255, 0, 255, 0]);
	const picture = decodeTga(bottomUp);
	assertEqual(list(picture.data), "255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255");
	assertEqual(picture.format, "TGA 24-bit");
	const topDown = decodeTga(tga(2, 2, 1, 24, 32, [0, 0, 255, 0, 255, 0]));
	assertEqual(list(topDown.data), "255,0,0,255,0,255,0,255");
	const rle = decodeTga(tga(10, 3, 1, 32, 32 | 8, [0x81, 1, 2, 3, 200, 0x00, 9, 8, 7, 100]));
	assertEqual(list(rle.data), "3,2,1,200,3,2,1,200,7,8,9,100");
	assertEqual(rle.format, "TGA 32-bit RLE");
});

test("filetypes: a TGA's color map, 16-bit pixels and a 32-bit image written without alpha", () => {
	const mapped = decodeTga(tga(1, 2, 1, 8, 32, [1, 0], [0, 0, 255, 255, 0, 0]));
	assertEqual(list(mapped.data), "0,0,255,255,255,0,0,255");
	const sixteen = decodeTga(tga(2, 2, 1, 16, 32 | 1, [0x00, 0xfc, 0x1f, 0x00]));
	assertEqual(list(sixteen.data), "255,0,0,255,0,0,255,0", "the top bit is alpha when the descriptor gives one");
	const noAlpha = decodeTga(tga(2, 1, 1, 32, 32, [1, 2, 3, 0]));
	assertEqual(list(noAlpha.data), "3,2,1,255");
	assertThrows(() => decodeTga(tga(2, 2, 2, 24, 0, [1, 2, 3])), Error, "pixels that end early");
});

// A DDS: `pixelFormat` [flags, fourCC, bits, r, g, b, a], or `dxgi` for a DX10 header.
function dds({ width, height, levels = 1, pixelFormat, dxgi, cube, data }) {
	const header = new DataView(new ArrayBuffer(128 + (dxgi !== undefined ? 20 : 0)));
	header.setUint32(0, 0x20534444, true);
	header.setUint32(4, 124, true);
	header.setUint32(8, 0x1007 | (levels > 1 ? 0x20000 : 0), true);
	header.setUint32(12, height, true);
	header.setUint32(16, width, true);
	header.setUint32(28, levels, true);
	header.setUint32(76, 32, true);
	const pf = dxgi !== undefined ? [0x4, 0x30315844, 0, 0, 0, 0, 0] : pixelFormat;
	pf.forEach((v, i) => header.setUint32(80 + i * 4, v >>> 0, true));
	if (cube && dxgi === undefined) header.setUint32(112, 0xfe00, true);
	if (dxgi !== undefined) {
		header.setUint32(128, dxgi, true);
		header.setUint32(132, 3, true);
		header.setUint32(136, cube ? 4 : 0, true);
		header.setUint32(140, 1, true);
	}
	const bytes = new Uint8Array(header.byteLength + data.length);
	bytes.set(new Uint8Array(header.buffer));
	bytes.set(data, header.byteLength);
	return bytes;
}

test("filetypes: a DDS's BC1 block, its four colors", () => {
	// Red and blue endpoints; the first row's texels take colors 0, 1, 2, 3.
	const texture = readDds(dds({ width: 4, height: 4, pixelFormat: [0x4, 0x31545844], data: [0x00, 0xf8, 0x1f, 0x00, 0xe4, 0, 0, 0] }));
	assertEqual(texture.format, "BC1 (DXT1)");
	const row = Array.from(texture.image(0, 0).data.subarray(0, 16)).join(",");
	assertEqual(row, "255,0,0,255,0,0,255,255,170,0,85,255,85,0,170,255");
});

test("filetypes: a DDS's mips, cube faces and float formats", () => {
	const argb = [0x41, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000];
	const mips = readDds(dds({ width: 2, height: 2, levels: 2, pixelFormat: argb, data: [1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 10, 20, 30, 40] }));
	assertEqual([mips.levels, mips.format].join(" "), "2 RGBA 32-bit");
	const small = mips.image(0, 1);
	assertEqual([small.width, small.height, list(small.data)].join(" "), "1 1 30,20,10,40");
	const faces = readDds(dds({ width: 1, height: 1, dxgi: 28, cube: true, data: [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5] }));
	assertEqual([faces.layers, faces.layerName(3), list(faces.image(3, 0).data)].join(" "), "6 -Y 3,3,3,3");
	const floats = readDds(dds({ width: 1, height: 1, dxgi: 10, data: [0x00, 0x3c, 0x00, 0x38, 0x00, 0x40, 0x00, 0x3c] }));
	assertTrue(floats.hdr, "a float format is HDR");
	assertEqual(list(floats.image(0, 0).data), "1,0.5,2,1");
	assertThrows(() => readDds(dds({ width: 1, height: 1, dxgi: 200, data: [0, 0, 0, 0] })), Error, "an unknown DXGI format");
});

test("filetypes: half floats and R11G11B10", () => {
	assertEqual([halfToFloat(0x3c00), halfToFloat(0xc000), halfToFloat(0x0001), halfToFloat(0x7c00)].join(","), "1,-2," + Math.pow(2, -24) + ",Infinity");
	assertEqual(r11g11b10((15 << 6) | (15 << 17) | (15 << 27)).join(","), "1,1,1");
});

test("filetypes: a Radiance HDR's flat and run-length scanlines", () => {
	const text = (s) => Array.from(s, (c) => c.charCodeAt(0));
	const flat = Uint8Array.from(text("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n").concat([128, 64, 0, 129, 9, 9, 9, 0]));
	const picture = decodeHdr(flat);
	assertEqual(list(picture.data), "1,0.5,0,1,0,0,0,1");
	const runs = [2, 2, 0, 8, 136, 128, 136, 128, 136, 128, 136, 129];
	const coded = decodeHdr(Uint8Array.from(text("#?RGBE\n\n+Y 1 +X 8\n").concat(runs)));
	assertEqual(coded.data.slice(28, 32).join(","), "1,1,1,1");
	assertThrows(() => decodeHdr(Uint8Array.from(text("#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 1 +X 1\n0000"))), Error, "XYZE");
});

const BC7 = [
	["1f5180f383a5dcf31ae239e5999f8e6b", "8c9eccff8cdfe3ff1157c2ff29ce7bff94e710ff8cdfe3ff8c9eccff0d40d0ffb19107ffb19107ff8cbed8ff8c189cffc65200ff94e710ffb87c05ff8c9eccff"],
	["ca928cd7bbc6c47dc0c596703d009d14", "5adad3ff5adad3ff5adad3ff93a06aff7eb58fffa58d48ffde7196ff5adad3ff48edf5ffe33272ffdc88a3ffd9b2bbff5adad3ff5adad3ffda9dafff48edf5ff"],
	["1c49d1197302d0e4af7dad5035654059", "21216bffce9c52ffc6e742ff24169cff24169cffd642d6ffe77badff21216bff21216bffd642d6ffc6e742ff24169cff24169cffd17e7dffd1c465ff24169cff"],
	["f8ffed5bce60ffbe83a313b957168e89", "f855b6ff62eaa7fff3a8adfffe06c0ffedf7a3ff62eaa7fffe06c0fffe06c0ffedf7a3ff8ee465ffb7df27ff39efe5fffe06c0ff8ee465ffb7df27ff8ee465ff"],
	["50497524e0a5b4b7934f06d9b55e5d76", "4d4736b64f5d5e7b4d3e36b6523e8442523784424f475e7b4d3736b6523784424d2f36b64f475e7b4a3710ef522f8442523784424d3e36b64f375e7b4a4710ef"],
	["601766e4958d7fde1d6c81cdc0dd99e0", "9f36c3519f5ee3999f22b32e7722b32e924ad3767736c351925ee399774ad3769222b32e8422b32e9222b32e845ee3999f4ad3769f36c351844ad3767736c351"],
	["4065ea8642d86b90edfb7a8384ce0693", "7a636a4d5753df266559b3355351ed21695ba5397661784888693e5c725f864483674b57725f86445753df266057c0307a636a4d956f116b88693e5c6e5d943f"],
	["80c421d10cf6b485e3ab1ab79f6d7824", "3131a470246586ae3818b251246586ae2b4c948f3131a470246586ae8e7d1c8e3818b251246586ae246586ae8e7d1c8e2b4c948f3818b251709b66a98e7d1c8e"],
];

test("filetypes: BC7, a block of each mode as Pillow decodes it", () => {
	const texels = new Uint8Array(64);
	BC7.forEach(([block, expected], mode) => {
		decodeBc7Block(hex(block), 0, texels);
		assertEqual(list(texels), list(hex(expected)), "mode " + mode);
	});
	decodeBc7Block(new Uint8Array(16), 0, texels);
	assertEqual(list(texels.subarray(0, 4)), "0,0,0,0", "a reserved mode is transparent black");
});

const BC6H = [
	["78e23ad9e21fa0b374ac78b6edf5fcfa", "ffff11ffffff13ffffff0dffffff0dffffff12ffffff0dffffff0dffffff0dffffff11ffffff0dffffff0dffffff0dffffff0dffffff0dffffff0dffffff0dff"],
	["89408f66abff4e8ccca580b27acee266", "00012eff00012eff07380aff422d1bffff00ffffff1bcaff005b01ffff15ffffff00ffff422d1bff07380affff10ffff005b01ff004603ff422d1bffff15ffff"],
	["62334bd444020826a7e198f575cd85ed", "000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff000005ff"],
	["665ceb3342b17c7782c4d37fce6f8ea9", "13ff00ff11ff00ff12ff00ff11ff00ff11ff00ff11ff00ff12ff00ff11ff00ff11ff00ff11ff00ff12ff00ff11ff00ff13ff00ff12ff00ff12ff00ff11ff00ff"],
	["ea08d44a5175eacf1970b0b7c8f68342", "00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff"],
	["6eecf72ef76cd7ba7af010a644e77f5e", "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"],
	["1231bfdb692573ea03170073948beed6", "ffffffffffffffffd5eeffffffbafffffff9ffffffe9ffffff83ffffd5eeffffefd9fffffffffffffed4ffffff9dffff9cbbffffc5caffffff83ffffff9dffff"],
	["169ccb323f1e9e0bc08bf444abc1a25b", "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"],
	["3a0150aa9593c4edf3dc7db0b8f0228b", "60ffffffffffffff00ffffffffffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff00ffffff"],
	["3e799621f599e0b1814a156aab28ef22", "02ff05ff02ff05ffff0a53ff1dff0cffff581dffff581dffff01ccff0073c7ffff0a53ff0073c7ff0373ffff0073d8ff0b73ffff0073c7ff0073ebff0073a4ff"],
	["c3c8ea964e816963ce2685ce326003f5", "07ffffff00ff07ff0fffffffffffffff28ffffff03ffffff00ff01ff00ff07ffffffffffa6ffffffff9affff0fffffffa6ffffffff9affff28ffffff00ff00ff"],
	["0763e3120dd558a55aefc5f106068279", "ffff03ffffff03ffffff01ffffff01ffffff03ffffff01ffffff06ffffff01ffffff03ffffff07ffffff03ffffff07ffffff05ffffff02ffffff02ffffff03ff"],
	["4bae4d7353e91f7ff277929ece2b2797", "ff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ffff0000ff"],
	["8f346f9f5675993badb5118c660832a5", "0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff0a00ffff"],
];

test("filetypes: BC6H, unsigned blocks of each mode as Pillow decodes them (clamped to 0..1, times 255)", () => {
	const texels = new Float32Array(64);
	for (const [block, expected] of BC6H) {
		decodeBc6hBlock(hex(block), 0, texels, false);
		const shown = Array.from(texels, (v, i) => (i % 4 === 3 ? 255 : v < 0 ? 0 : v > 1 ? 255 : Math.floor(v * 255)));
		const want = Array.from(hex(expected));
		assertTrue(shown.every((v, i) => Math.abs(v - want[i]) <= 1), "block " + block + ": " + shown.join(",") + " vs " + want.join(","));
	}
});

test("filetypes: the image view fits, zooms about a point, steps, keeps the picture in sight", () => {
	const view = { width: 800, height: 600 };
	const big = fitted(view, { width: 4096, height: 4096 });
	assertEqual([big.zoom, big.x, big.y].join(","), [600 / 4096, (4096 - 800 / (600 / 4096)) / 2, 0].join(","));
	assertEqual(JSON.stringify(fitted(view, { width: 100, height: 50 })), JSON.stringify({ zoom: 1, x: -350, y: -275 }), "a small picture is not enlarged");
	assertEqual(JSON.stringify(zoomedAt({ zoom: 1, x: 0, y: 0 }, 4, 100, 50)), JSON.stringify({ zoom: 4, x: 75, y: 37.5 }), "the texel under the point stays");
	assertEqual([steppedZoom(1, 1), steppedZoom(1, -1), steppedZoom(256, 1), steppedZoom(1 / 64, -1)].join(","), [1.5, 2 / 3, 256, 1 / 64].join(","));
	const image = { width: 100, height: 100 };
	assertEqual(JSON.stringify(texelAt({ zoom: 2, x: 10, y: 20 }, image, 5, 7)), JSON.stringify({ x: 12, y: 23 }));
	assertEqual(texelAt({ zoom: 2, x: 10, y: 20 }, image, -30, 0), null, "off the picture");
	assertEqual(JSON.stringify(kept({ zoom: 1, x: 500, y: 0 }, view, { width: 100, height: 50 })), JSON.stringify({ zoom: 1, x: -350, y: -275 }), "a picture that fits is centered");
	assertEqual(JSON.stringify(kept({ zoom: 1, x: 5000, y: -900 }, view, { width: 2000, height: 2000 })), JSON.stringify({ zoom: 1, x: 1600, y: -300 }), "no edge past the middle");
	assertEqual(JSON.stringify(rescaled({ zoom: 0.5, x: 100, y: 40 }, { width: 1024, height: 1024 }, { width: 64, height: 64 })), JSON.stringify({ zoom: 8, x: 6.25, y: 2.5 }), "a smaller mip shows the same area");
});

test("filetypes: animated GIF, PNG and WebP are told from still ones", () => {
	const gce = [0x21, 0xf9, 0x04, 0, 0, 0, 0, 0];
	assertEqual(isAnimated("gif", Uint8Array.from([0x47, 0x49, 0x46].concat(gce, gce))), true);
	assertEqual(isAnimated("gif", Uint8Array.from([0x47, 0x49, 0x46].concat(gce))), false);
	const chunk = (type) => [0, 0, 0, 0].concat(Array.from(type, (c) => c.charCodeAt(0)), [0, 0, 0, 0]);
	const signature = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
	assertEqual(isAnimated("png", Uint8Array.from(signature.concat(chunk("IHDR"), chunk("acTL"), chunk("IDAT")))), true);
	assertEqual(isAnimated("png", Uint8Array.from(signature.concat(chunk("IHDR"), chunk("IDAT"), chunk("acTL")))), false, "acTL after the image data does not count");
	const webp = (flags) => Uint8Array.from(Array.from("RIFF\0\0\0\0WEBPVP8X\0\0\0\0", (c) => c.charCodeAt(0)).concat([flags, 0, 0, 0]));
	assertEqual([isAnimated("webp", webp(0x02)), isAnimated("webp", webp(0x10))].join(","), "true,false");
});

test("filetypes: the texel readout writes bytes with their hex, floats with four decimals", () => {
	assertEqual(texelText({ x: 3, y: 4 }, [255, 128, 0, 255], false), "X 3  Y 4   R 255  G 128  B 0  A 255   #FF8000FF");
	assertEqual(texelText({ x: 0, y: 0 }, [1, 0, 0, 1], true), "X 0  Y 0   R 1.0000  G 0.0000  B 0.0000  A 1.0000", "whole floats are floats");
	assertEqual(texelText({ x: 1, y: 2 }, null, false), "X 1  Y 2");
});

test("filetypes: hex rows, offsets typed, and the data inspector", () => {
	assertEqual([offsetDigits(100), offsetDigits(Math.pow(2, 32) + 1)].join(","), "8,10");
	assertEqual([bytesPerRow(80, 8), bytesPerRow(50, 8), bytesPerRow(30, 8)].join(","), "16,8,4");
	assertEqual(["0x1F", "1Fh", "$10", "31", "zz", "0x1000"].map((t) => parseOffset(t, 100)).join(","), "31,31,16,31,,99");
	const one = Object.fromEntries(inspect(Uint8Array.of(0x00, 0x00, 0x80, 0x3f), true));
	assertEqual([one.Float32, one.UInt16, one.Int8, one.Int64].join(","), "1,0,0,-");
	assertEqual(Object.fromEntries(inspect(Uint8Array.of(0x00, 0x00, 0x80, 0x3f), false)).UInt32, "32831", "big-endian");
	assertTrue(Object.fromEntries(inspect(Uint8Array.of(0xe2, 0x82, 0xac), true))["UTF-8"].startsWith("U+20AC"));
	assertEqual(Object.fromEntries(inspect(Uint8Array.of(0x80), true))["UTF-8"], "-", "a continuation byte begins no character");
});

test("filetypes: a font's names, a range's total, sizes", () => {
	const strings = [0, 0x54, 0, 0x65, 0, 0x73, 0, 0x74].concat(Array.from("Bold", (c) => c.charCodeAt(0)));
	const record = (platform, encoding, language, id, length, offset) => [platform, encoding, language, id, length, offset].flatMap((v) => [v >> 8, v & 255]);
	const table = Uint8Array.from([0, 0, 0, 2, 0, 30].concat(record(3, 1, 0x409, 1, 8, 0), record(1, 0, 0, 2, 4, 8), strings));
	assertEqual(JSON.stringify(readNames(table)), JSON.stringify({ 1: "Test", 2: "Bold" }));
	assertEqual([totalOf("bytes 0-99/1234"), totalOf("bytes */0"), totalOf(null)].join(","), "1234,0,-1");
	assertEqual([formatSize(512), formatSize(1536), formatSize(10 * 1024 * 1024), formatSize(300 * 1024 * 1024)].join(","), "512 B,1.5 KB,10.0 MB,300 MB");
});
