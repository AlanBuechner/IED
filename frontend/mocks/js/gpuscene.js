// gpuscene.js - the "captured frame" for the GPU debugger mocks. A tiny ray caster renders a courtyard
// (ground, crates, barrel, pillar, far props, wall) at a quarter of 1920x1080 into per-pixel buffers, then
// any <canvas data-scene="mode"> is filled from them. Frame pixels are content, not UI color: they are
// numbers written into ImageData. Overlay colors (NaN highlight, selection outline) come from theme tokens.
// The story: Crate_03's material binds the engine's black texture as roughness, so GGX divides 0 by 0 where
// N.H is ~1 and the lit pass writes NaN (black specks after the blit).
"use strict";

(() => {
	const W = 480;
	const H = 270;
	const SCALE = 4;
	const SELECTED = 4;

	const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
	const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
	const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
	const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
	const norm = (a) => mul(a, 1 / Math.hypot(a[0], a[1], a[2]));

	const SUN = norm([0.55, 0.78, -0.42]);
	const CAM = { pos: [0.3, 2.0, -5.0], target: [0.8, 0.8, 3.0], fov: 50 };

	const OBJECTS = [
		{ id: 1, name: "Ground", kind: "plane", albedo: [0.40, 0.38, 0.35], rough: 0.9 },
		{ id: 2, name: "Crate_01", kind: "box", min: [-2.9, 0, 1.6], max: [-1.9, 1.0, 2.6], albedo: [0.52, 0.36, 0.21], rough: 0.7, planks: true },
		{ id: 3, name: "Crate_02", kind: "box", min: [-1.5, 0, 3.4], max: [-0.5, 1.0, 4.4], albedo: [0.48, 0.34, 0.20], rough: 0.7, planks: true },
		{ id: 4, name: "Crate_03", kind: "box", min: [1.0, 0, 2.2], max: [2.2, 1.2, 3.4], albedo: [0.56, 0.40, 0.24], rough: 0.0, planks: true },
		{ id: 5, name: "Barrel", kind: "sphere", c: [-0.6, 0.45, 1.0], r: 0.45, albedo: [0.30, 0.31, 0.33], rough: 0.35 },
		{ id: 6, name: "Pillar_A", kind: "box", min: [3.3, 0, 6.0], max: [4.1, 3.4, 6.8], albedo: [0.55, 0.53, 0.49], rough: 0.8 },
		{ id: 7, name: "Prop_far", kind: "box", min: [-6.2, 0, 15.0], max: [-5.0, 1.2, 16.2], albedo: [0.50, 0.35, 0.22], rough: 0.7, planks: true },
		{ id: 7, name: "Prop_far", kind: "box", min: [4.6, 0, 19.0], max: [6.0, 1.4, 20.4], albedo: [0.47, 0.33, 0.20], rough: 0.7, planks: true },
		{ id: 8, name: "Wall", kind: "box", min: [-14, 0, 30], max: [14, 4.5, 31], albedo: [0.46, 0.44, 0.41], rough: 0.8 },
	];

	function hitBox(o, d, b) {
		let t0 = -Infinity;
		let t1 = Infinity;
		let axis = 0;
		for (let i = 0; i < 3; i++) {
			const inv = 1 / d[i];
			let a = (b.min[i] - o[i]) * inv;
			let c = (b.max[i] - o[i]) * inv;
			if (a > c) [a, c] = [c, a];
			if (a > t0) { t0 = a; axis = i; }
			t1 = Math.min(t1, c);
			if (t0 > t1) return null;
		}
		if (t0 < 1e-4) return null;
		const n = [0, 0, 0];
		n[axis] = d[axis] > 0 ? -1 : 1;
		return { t: t0, n };
	}

	function hitSphere(o, d, s) {
		const oc = sub(o, s.c);
		const b = dot(oc, d);
		const c = dot(oc, oc) - s.r * s.r;
		const h = b * b - c;
		if (h < 0) return null;
		const t = -b - Math.sqrt(h);
		if (t < 1e-4) return null;
		return { t, n: norm(sub(add(o, mul(d, t)), s.c)) };
	}

	function trace(o, d) {
		let best = null;
		for (const obj of OBJECTS) {
			let h = null;
			if (obj.kind === "plane") h = d[1] < 0 ? { t: -o[1] / d[1], n: [0, 1, 0] } : null;
			else if (obj.kind === "box") h = hitBox(o, d, obj);
			else h = hitSphere(o, d, obj);
			if (h && h.t > 1e-4 && (!best || h.t < best.t)) best = { t: h.t, n: h.n, obj };
		}
		return best;
	}

	function albedoAt(obj, p, n) {
		if (obj.kind === "plane") {
			const checker = (Math.floor(p[0]) + Math.floor(p[2])) & 1;
			const grout = Math.min(Math.abs(p[0] - Math.round(p[0])), Math.abs(p[2] - Math.round(p[2]))) < 0.03 ? 0.6 : 1;
			const k = (0.86 + 0.14 * checker) * grout;
			return mul(obj.albedo, k);
		}
		if (obj.planks) {
			const u = Math.abs(n[1]) > 0.5 ? p[0] : (Math.abs(n[0]) > 0.5 ? p[2] : p[0]);
			const v = Math.abs(n[1]) > 0.5 ? p[2] : p[1];
			const plank = (v * 4) - Math.floor(v * 4);
			const gap = plank < 0.07 ? 0.45 : 1;
			const grain = 0.88 + 0.12 * Math.sin(u * 23 + Math.sin(v * 9) * 2);
			const edge = Math.min(p[0] - obj.min[0], obj.max[0] - p[0], p[1] - obj.min[1], obj.max[1] - p[1], p[2] - obj.min[2], obj.max[2] - p[2]);
			const frame = edge < 0.07 ? 0.7 : 1;
			return mul(obj.albedo, gap * grain * frame);
		}
		return obj.albedo;
	}

	function sky(d) {
		const t = Math.max(0, d[1]);
		const horizon = [0.50, 0.55, 0.60];
		const zenith = [0.22, 0.32, 0.50];
		return add(mul(horizon, 1 - t), mul(zenith, t));
	}

	// ---------- render once into buffers ----------
	const N = W * H;
	const hdr = new Float32Array(N * 3);
	const fixed = new Float32Array(N * 3);
	const depth = new Float32Array(N);
	const normals = new Float32Array(N * 3);
	const albedo = new Float32Array(N * 3);
	const ids = new Uint8Array(N);
	const nan = new Uint8Array(N);
	const crateNdh = [];

	const f = norm(sub(CAM.target, CAM.pos));
	const r = norm(cross([0, 1, 0], f));
	const u = cross(f, r);
	const tanH = Math.tan(CAM.fov * Math.PI / 360);
	const aspect = W / H;

	for (let py = 0; py < H; py++) {
		for (let px = 0; px < W; px++) {
			const i = py * W + px;
			const x = (2 * (px + 0.5) / W - 1) * aspect * tanH;
			const y = (1 - 2 * (py + 0.5) / H) * tanH;
			const d = norm(add(f, add(mul(r, x), mul(u, y))));
			const h = trace(CAM.pos, d);
			let c;
			if (!h) {
				c = sky(d);
				depth[i] = 1;
				fixed.set(c, i * 3);
			} else {
				const p = add(CAM.pos, mul(d, h.t));
				const n = h.n;
				const a = albedoAt(h.obj, p, n);
				const ndl = Math.max(0, dot(n, SUN));
				const lit = ndl > 0 && !trace(add(p, mul(n, 1e-3)), SUN) ? 1 : 0;
				const v = mul(d, -1);
				const hv = norm(add(SUN, v));
				const ndh = Math.max(0, dot(n, hv));
				const rough = h.obj.rough;
				const specFixed = Math.pow(ndh, 8 + (1 - Math.max(rough, 0.045)) * 160) * (1 - Math.max(rough, 0.045)) * 1.6;
				const ambient = mul(a, 0.14 + 0.06 * n[1]);
				const diffuse = mul(a, 1.55 * ndl * lit);
				c = add(ambient, add(diffuse, [specFixed * lit, specFixed * lit, specFixed * lit]));
				fixed.set(c, i * 3);
				if (h.obj.id === SELECTED && lit) crateNdh.push([ndh, i]);
				depth[i] = Math.min(1, dot(sub(p, CAM.pos), f) / 60);
				normals.set([n[0] * 0.5 + 0.5, n[1] * 0.5 + 0.5, n[2] * 0.5 + 0.5], i * 3);
				albedo.set(a, i * 3);
				ids[i] = h.obj.id;
			}
			hdr.set(c, i * 3);
		}
	}

	// GGX with roughness 0: a2 / denom = 0 / 0 where N.H is ~1 -> NaN on the brightest highlight pixels
	crateNdh.sort((a, b) => b[0] - a[0]);
	for (const [, i] of crateNdh.slice(0, 26)) nan[i] = 1;
	const pickIndex = crateNdh.length ? crateNdh[0][1] : 0;
	const PICK = { x: pickIndex % W, y: Math.floor(pickIndex / W) };

	// ---------- display ----------
	function token(name) {
		const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
		const hex = v.startsWith("#") ? v.slice(1) : "808080";
		return [0, 2, 4].map((k) => parseInt(hex.slice(k, k + 2), 16));
	}

	const tonemap = (x) => Math.round(255 * Math.pow(x / (1 + x), 1 / 2.2));

	function pixelRgb(mode, i) {
		const k = i * 3;
		switch (mode) {
			case "depth": { const g = Math.round(255 * Math.pow(1 - depth[i], 1.6)); return [g, g, g]; }
			case "normal": return [normals[k] * 255, normals[k + 1] * 255, normals[k + 2] * 255];
			case "albedo": return [albedo[k] * 255, albedo[k + 1] * 255, albedo[k + 2] * 255];
			case "id": { const id = ids[i]; return id ? [(id * 97) % 200 + 40, (id * 57) % 200 + 40, (id * 131) % 200 + 40] : [0, 0, 0]; }
			case "fixed": return [tonemap(fixed[k]), tonemap(fixed[k + 1]), tonemap(fixed[k + 2])];
			default: return nan[i] ? [0, 0, 0] : [tonemap(hdr[k]), tonemap(hdr[k + 1]), tonemap(hdr[k + 2])];
		}
	}

	function fillFrame(ctx, mode, opts) {
		const img = ctx.createImageData(W, H);
		const err = token("--accent-error");
		const focus = token("--focus");
		for (let i = 0; i < N; i++) {
			let c = pixelRgb(mode, i);
			if (opts.highlight && ids[i] !== SELECTED) c = c.map((v) => v * 0.35);
			if (opts.nan && nan[i]) c = err;
			if (opts.highlight && ids[i] === SELECTED) {
				const x = i % W;
				const y = Math.floor(i / W);
				const edge = (x > 0 && ids[i - 1] !== SELECTED) || (x < W - 1 && ids[i + 1] !== SELECTED) || (y > 0 && ids[i - W] !== SELECTED) || (y < H - 1 && ids[i + W] !== SELECTED);
				if (edge) c = focus;
			}
			img.data.set([c[0], c[1], c[2], 255], i * 4);
		}
		ctx.putImageData(img, 0, 0);
	}

	function fillShadowMap(ctx, w, h) {
		const img = ctx.createImageData(w, h);
		const back = mul(SUN, -1);
		const ru = norm(cross([0, 1, 0], back));
		const uu = cross(back, ru);
		for (let y = 0; y < h; y++) {
			for (let x = 0; x < w; x++) {
				const o = add(add(add([1, 0, 8], mul(SUN, 40)), mul(ru, (x / w - 0.5) * 34)), mul(uu, (0.5 - y / h) * 34));
				const hit = trace(o, back);
				const g = hit ? Math.round(255 * Math.min(1, Math.max(0, (hit.t - 30) / 20))) : 255;
				img.data.set([g, g, g, 255], (y * w + x) * 4);
			}
		}
		ctx.putImageData(img, 0, 0);
	}

	function fillPlanks(ctx, w, h) {
		const img = ctx.createImageData(w, h);
		const obj = OBJECTS[3];
		for (let y = 0; y < h; y++) {
			for (let x = 0; x < w; x++) {
				const p = [obj.min[0] + (x / w) * 1.2, obj.min[1] + (1 - y / h) * 1.2, obj.min[2]];
				const a = albedoAt(obj, p, [0, 0, -1]);
				img.data.set([a[0] * 255, a[1] * 255, a[2] * 255, 255], (y * w + x) * 4);
			}
		}
		ctx.putImageData(img, 0, 0);
	}

	function fillFlat(ctx, w, h, v) {
		const img = ctx.createImageData(w, h);
		for (let i = 0; i < w * h; i++) img.data.set([v, v, v, 255], i * 4);
		ctx.putImageData(img, 0, 0);
	}

	function fillZoom(ctx, mode, opts, cells, size) {
		const half = Math.floor(cells / 2);
		const img = ctx.createImageData(cells * size, cells * size);
		const grid = token("--border-1");
		const focus = token("--focus");
		const err = token("--accent-error");
		for (let cy = 0; cy < cells; cy++) {
			for (let cx = 0; cx < cells; cx++) {
				const sx = Math.min(W - 1, Math.max(0, PICK.x + cx - half));
				const sy = Math.min(H - 1, Math.max(0, PICK.y + cy - half));
				const i = sy * W + sx;
				const base = opts.nan && nan[i] ? err : pixelRgb(mode, i);
				for (let y = 0; y < size; y++) {
					for (let x = 0; x < size; x++) {
						const border = x === size - 1 || y === size - 1;
						const center = cx === half && cy === half && (x === 0 || y === 0 || x === size - 2 || y === size - 2);
						const c = center ? focus : border ? grid : base;
						img.data.set([c[0], c[1], c[2], 255], ((cy * size + y) * cells * size + cx * size + x) * 4);
					}
				}
			}
		}
		ctx.putImageData(img, 0, 0);
	}

	function histogram(bins) {
		const out = new Array(bins).fill(0);
		for (let i = 0; i < N; i++) {
			const k = i * 3;
			const lum = 0.2126 * hdr[k] + 0.7152 * hdr[k + 1] + 0.0722 * hdr[k + 2];
			const b = Math.floor(lum / (1 + lum) * bins);
			out[Math.min(bins - 1, Math.max(0, b))]++;
		}
		const max = Math.max(...out);
		return out.map((v) => v / max);
	}

	function paint(canvas) {
		const mode = canvas.dataset.scene;
		const opts = { nan: canvas.hasAttribute("data-nan"), highlight: canvas.hasAttribute("data-highlight") };
		const ctx = canvas.getContext("2d");
		if (mode === "zoom" || mode === "zoom-fixed") {
			const cells = Number(canvas.dataset.cells || 11);
			const size = Number(canvas.dataset.cell || 9);
			canvas.width = cells * size;
			canvas.height = cells * size;
			fillZoom(ctx, mode === "zoom" ? "hdr" : "fixed", opts, cells, size);
			return;
		}
		if (mode === "shadow" || mode === "planks" || mode === "black" || mode === "white") {
			canvas.width = 96;
			canvas.height = 96;
			if (mode === "shadow") fillShadowMap(ctx, 96, 96);
			else if (mode === "planks") fillPlanks(ctx, 96, 96);
			else fillFlat(ctx, 96, 96, mode === "black" ? 0 : 255);
			return;
		}
		canvas.width = W;
		canvas.height = H;
		fillFrame(ctx, mode, opts);
	}

	(window.Mock = window.Mock || { hooks: [] }).hooks.push(() => {
		for (const c of document.querySelectorAll("canvas[data-scene]")) paint(c);
		for (const el of document.querySelectorAll("[data-pick]")) el.textContent = el.dataset.pick === "x" ? String(PICK.x * SCALE) : String(PICK.y * SCALE);
		for (const el of document.querySelectorAll(".pick-marker")) {
			el.style.left = ((PICK.x + 0.5) / W * 100) + "%";
			el.style.top = ((PICK.y + 0.5) / H * 100) + "%";
		}
		for (const c of document.querySelectorAll("canvas[data-swatch]")) {
			c.width = 1;
			c.height = 1;
			const ctx = c.getContext("2d");
			const img = ctx.createImageData(1, 1);
			const v = c.dataset.swatch === "nan" ? token("--accent-error") : c.dataset.swatch.split(",").map(Number);
			img.data.set([v[0], v[1], v[2], 255]);
			ctx.putImageData(img, 0, 0);
		}
		for (const el of document.querySelectorAll("[data-histogram]")) {
			el.innerHTML = histogram(Number(el.dataset.histogram)).map((v) => `<i style="height:${Math.max(2, v * 100).toFixed(1)}%"></i>`).join("");
		}
	});
})();
