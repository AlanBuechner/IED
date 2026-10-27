// languagetools.test.js - the language tools snapshot read for the banner and Settings > Languages
// (frontend/src/shell/toolsmodel.js, docs/26 "Language tools").

import { test, assertEqual } from "./harness.js";
import { installPlan, kindText, pinText, stateText, toolGroups, toolsBanner, userScopeQuestion } from "../src/shell/toolsmodel.js";

function tool(id, state, extra) {
	return Object.assign({ id, title: id.toUpperCase(), kind: "other", plugin: "ied.go", pluginName: "Go", state, path: "", version: "", source: "", pin: null, problem: "",
		canInstall: true, installs: "1.0", installsForUser: false, requires: [], homepage: "" }, extra || {});
}

const SNAPSHOT = {
	trusted: true, detected: true, detecting: false, install: null, busy: false, dismissed: false,
	tools: [
		tool("go", "found", { title: "Go", kind: "compiler", version: "1.26.6", source: "path", pin: { version: "1.22", match: "minimum", file: "go.mod" } }),
		tool("gopls", "missing", { title: "gopls", requires: ["go"] }),
		tool("dlv", "missing", { title: "Delve", requires: ["go"] }),
		tool("clangd", "found", { title: "clangd", plugin: "ied.cpp", pluginName: "C/C++", source: "bundled", version: "22.1.0", canInstall: false }),
	],
};

test("language tools: a tool's state and pin in words", () => {
	assertEqual(pinText(null), "");
	assertEqual(pinText({ version: "1.22", match: "minimum", file: "go.mod" }), "1.22 or newer (go.mod)");
	assertEqual(pinText({ version: "1.75.0", match: "exact", file: "rust-toolchain.toml" }), "1.75.0 (rust-toolchain.toml)");
	assertEqual(stateText(SNAPSHOT.tools[3]), { text: "22.1.0 (comes with IED)", tone: "ok" });
	assertEqual(stateText(tool("x", "found", { version: "1.2", source: "installed" })).text, "1.2 (installed by IED)");
	assertEqual(stateText(tool("x", "wrongVersion", { version: "1.20", pin: { version: "1.22", match: "minimum", file: "go.mod" } })), { text: "1.20 - the workspace needs 1.22 or newer (go.mod)", tone: "warn" });
	assertEqual(stateText(tool("x", "missing")).tone, "error");
	assertEqual(stateText(tool("x", "installing", { installs: "0.24.0" })).text, "Installing 0.24.0...");
	assertEqual(stateText(tool("x", "unchecked")).tone, "idle");
	assertEqual([kindText("languageServer"), kindText("debugAdapter"), kindText("nonsense")], ["Language server", "Debugger", "Tool"]);
});

test("language tools: an install takes what the tools need first", () => {
	assertEqual(installPlan(SNAPSHOT, []), ["gopls", "dlv"], "every missing one; go is found");
	const noGo = Object.assign({}, SNAPSHOT, { tools: SNAPSHOT.tools.map((t) => (t.id === "go" ? Object.assign({}, t, { state: "missing" }) : t)) });
	assertEqual(installPlan(noGo, ["dlv"]), ["go", "dlv"], "go before what needs it");
	assertEqual(installPlan(noGo, []), ["go", "gopls", "dlv"], "each once");
	assertEqual(installPlan(SNAPSHOT, ["clangd"]), [], "one IED cannot install is left out");
	assertEqual(installPlan(null, []), []);
});

test("language tools: an install for the user is asked first", () => {
	const rust = { tools: [tool("rustup", "missing", { title: "rustup", installsForUser: true }), tool("cargo", "missing", { title: "Rust toolchain (cargo)", installsForUser: true }), tool("x", "missing")] };
	assertEqual(userScopeQuestion(rust, ["x"]), null, "IED's own folder: nothing to ask");
	const question = userScopeQuestion(rust, ["rustup", "cargo", "x"]);
	assertEqual(question.confirm, "Install");
	assertEqual(question.message.startsWith("rustup, Rust toolchain (cargo) install the way their makers do"), true, question.message);
});

test("language tools: the workspace banner", () => {
	const banner = toolsBanner(SNAPSHOT);
	assertEqual([banner.tone, banner.install, banner.text], ["warn", ["gopls", "dlv"], "gopls and Delve are not installed."]);
	assertEqual(toolsBanner(Object.assign({}, SNAPSHOT, { dismissed: true })), null, "dismissed");
	assertEqual(toolsBanner(Object.assign({}, SNAPSHOT, { trusted: false })), null, "untrusted: nothing was looked for");
	assertEqual(toolsBanner(Object.assign({}, SNAPSHOT, { detected: false })), null, "before the first detection");
	const wrong = { trusted: true, detected: true, tools: [tool("go", "wrongVersion", { title: "Go", version: "1.20.1", pin: { version: "1.22", match: "minimum", file: "go.mod" } })] };
	assertEqual(toolsBanner(wrong).text, "Go 1.20.1 is not the 1.22 or newer (go.mod) the workspace needs.");
	const manual = { trusted: true, detected: true, tools: [tool("clangd", "missing", { title: "clangd", canInstall: false })] };
	assertEqual([toolsBanner(manual).install, toolsBanner(manual).text], [[], "clangd is not installed. Settings > Languages says how to get it."]);
	const installing = Object.assign({}, SNAPSHOT, { install: { tools: ["gopls", "dlv"], current: "dlv" } });
	assertEqual([toolsBanner(installing).busy, toolsBanner(installing).text], [true, "Installing Delve (2 of 2)..."]);
	const fine = { trusted: true, detected: true, tools: [SNAPSHOT.tools[0]] };
	assertEqual(toolsBanner(fine), null, "nothing lacking");
});

test("language tools: grouped by language", () => {
	assertEqual(toolGroups(SNAPSHOT).map((g) => [g.title, g.tools.map((t) => t.id)]), [["Go", ["go", "gopls", "dlv"]], ["C/C++", ["clangd"]]]);
});
