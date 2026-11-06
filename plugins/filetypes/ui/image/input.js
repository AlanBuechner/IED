// input.js - a picture view's gestures, in view coordinates: the wheel (and a touchpad's pinch) zooms about the
// pointer, a drag pans, two fingers pinch and pan together, a double click switches between fitting and actual size,
// the mouse's position or a tap picks a texel. Keys: + and - zoom, 0 fits, 1 is actual size, arrows pan.
//     const detach = attachInput(stage, { zoomBy, panBy, toggleFit, hover, pick, key });

const DRAG_PX = 4;
const KEY_PAN_PX = 48;
const KEYS = { "+": "zoomIn", "=": "zoomIn", "-": "zoomOut", "_": "zoomOut", "0": "fit", "1": "actual" };
const ARROWS = { ArrowLeft: [KEY_PAN_PX, 0], ArrowRight: [-KEY_PAN_PX, 0], ArrowUp: [0, KEY_PAN_PX], ArrowDown: [0, -KEY_PAN_PX] };

export function attachInput(stage, handlers) {
	const pointers = new Map();
	let pinch = null;
	let start = null;
	let moved = false;
	const local = (event) => {
		const box = stage.getBoundingClientRect();
		return [event.clientX - box.left, event.clientY - box.top];
	};
	const pinchOf = () => {
		const [a, b] = [...pointers.values()];
		return { d: Math.max(1, Math.hypot(a[0] - b[0], a[1] - b[1])), x: (a[0] + b[0]) / 2, y: (a[1] + b[1]) / 2 };
	};
	const listeners = {
		wheel: (event) => {
			event.preventDefault();
			const [px, py] = local(event);
			const delta = event.deltaMode === 1 ? event.deltaY * 16 : event.deltaMode === 2 ? event.deltaY * 400 : event.deltaY;
			handlers.zoomBy(Math.pow(2, -delta / (event.ctrlKey ? 100 : 300)), px, py);
		},
		pointerdown: (event) => {
			if (event.pointerType === "mouse" && event.button !== 0) return;
			stage.setPointerCapture(event.pointerId);
			pointers.set(event.pointerId, local(event));
			if (pointers.size === 1) {
				start = local(event);
				moved = false;
			}
			if (pointers.size === 2) pinch = pinchOf();
		},
		pointermove: (event) => {
			const at = local(event);
			if (event.pointerType === "mouse") handlers.hover(at[0], at[1]);
			const last = pointers.get(event.pointerId);
			if (!last) return;
			pointers.set(event.pointerId, at);
			if (pointers.size === 1) {
				if (!moved && Math.hypot(at[0] - start[0], at[1] - start[1]) > DRAG_PX) moved = true;
				if (moved) handlers.panBy(at[0] - last[0], at[1] - last[1]);
			}
			else if (pointers.size === 2 && pinch) {
				const next = pinchOf();
				handlers.zoomBy(next.d / pinch.d, next.x, next.y);
				handlers.panBy(next.x - pinch.x, next.y - pinch.y);
				pinch = next;
				moved = true;
			}
		},
		pointerup: (event) => {
			if (!pointers.delete(event.pointerId)) return;
			if (pointers.size === 0 && !moved) handlers.pick(...local(event));
			if (pointers.size < 2) pinch = null;
		},
		pointercancel: (event) => {
			pointers.delete(event.pointerId);
			pinch = null;
		},
		pointerleave: (event) => {
			if (event.pointerType === "mouse" && !pointers.size) handlers.hover(null, null);
		},
		dblclick: (event) => handlers.toggleFit(...local(event)),
		keydown: (event) => {
			if (event.ctrlKey || event.metaKey || event.altKey) return;
			const name = KEYS[event.key];
			const arrow = ARROWS[event.key];
			if (!name && !arrow) return;
			event.preventDefault();
			if (name) handlers.key(name);
			else handlers.panBy(arrow[0], arrow[1]);
		},
	};
	for (const [type, fn] of Object.entries(listeners)) stage.addEventListener(type, fn, type === "wheel" ? { passive: false } : undefined);
	return () => {
		for (const [type, fn] of Object.entries(listeners)) stage.removeEventListener(type, fn);
	};
}
