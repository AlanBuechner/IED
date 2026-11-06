// viewport.js - where a picture sits in its view: `zoom` (view pixels a texel) and `x`, `y` (the texel at the view's
// top-left corner). Fitting, zooming about a point, the zoom steps of the buttons, panning that keeps the picture in
// sight, and the texel under a point.
//     let state = fitted({ width: 800, height: 600 }, { width: 4096, height: 4096 });
//     state = zoomedAt(state, state.zoom * 2, 400, 300);   // the texel under (400, 300) stays there

const MIN_ZOOM = 1 / 64;
const MAX_ZOOM = 256;
const STEPS = [1 / 64, 1 / 32, 1 / 16, 1 / 8, 1 / 4, 1 / 3, 1 / 2, 2 / 3, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256];

const clampZoom = (zoom) => Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, zoom));

function centered(view, image, zoom) {
	return { zoom, x: (image.width - view.width / zoom) / 2, y: (image.height - view.height / zoom) / 2 };
}

// The whole picture in the view, never enlarged past its size unless `grow`.
export function fitted(view, image, grow) {
	const fit = Math.min(view.width / image.width, view.height / image.height);
	return centered(view, image, clampZoom(grow ? fit : Math.min(1, fit)));
}

export function zoomedAt(state, zoom, px, py) {
	const z = clampZoom(zoom);
	return { zoom: z, x: state.x + px / state.zoom - px / z, y: state.y + py / state.zoom - py / z };
}

// The next zoom step above (`direction` 1) or below (-1) `zoom`.
export function steppedZoom(zoom, direction) {
	if (direction > 0) return STEPS.find((s) => s > zoom * 1.001) || MAX_ZOOM;
	for (let i = STEPS.length - 1; i >= 0; i--) if (STEPS[i] < zoom / 1.001) return STEPS[i];
	return MIN_ZOOM;
}

export function panned(state, dx, dy) {
	return { zoom: state.zoom, x: state.x - dx / state.zoom, y: state.y - dy / state.zoom };
}

// `state` with the picture kept in sight: centered on an axis it fits, else no edge further in than the view's middle.
export function kept(state, view, image) {
	const axis = (offset, viewLength, imageLength) => {
		const shown = viewLength / state.zoom;
		if (imageLength <= shown) return (imageLength - shown) / 2;
		return Math.min(imageLength - shown / 2, Math.max(-shown / 2, offset));
	};
	return { zoom: state.zoom, x: axis(state.x, view.width, image.width), y: axis(state.y, view.height, image.height) };
}

// The texel under view point (px, py); null off the picture.
export function texelAt(state, image, px, py) {
	const x = Math.floor(state.x + px / state.zoom);
	const y = Math.floor(state.y + py / state.zoom);
	return x >= 0 && y >= 0 && x < image.width && y < image.height ? { x, y } : null;
}

// The same picture area shown after the picture's size changes (another mip level).
export function rescaled(state, from, to) {
	const k = to.width / from.width;
	return { zoom: clampZoom(state.zoom / k), x: state.x * k, y: state.y * (to.height / from.height) };
}

export function zoomLabel(zoom) {
	const percent = zoom * 100;
	return (percent >= 10 ? Math.round(percent) : percent.toFixed(1)) + "%";
}
