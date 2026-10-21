// main.js - the web client's entry: one shell for this tab (docs/15 "The web client as built").

import { Shell } from "./shell/shell.js";

const root = document.getElementById("app");
root.classList.remove("booting");
const shell = new Shell(root);
shell.start().then(() => {
	document.documentElement.dataset.ready = "1";
}, (error) => {
	root.textContent = "IED could not start: " + error.message;
	console.error(error);
});
window.ied_shell = shell;
