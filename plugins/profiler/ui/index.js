// The profiler's UI module (docs/16, mockup 06): filled in as the views land.
export function activate(ied) {
	ied.commands.handle("ied.profiler.show", () => ied.commands.run("shell.showActivity", { id: "ied.profiler" }));
}

export function CaptureLibrary(ctx) {
	ctx.el.textContent = "Captures";
	return { dispose() {} };
}
