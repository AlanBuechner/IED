// fileicons.test.js - file and project icons from the plugins' fileIcons and projectIcons (docs/15 "Icons"): which
// rule wins for a file, how a project finds its language, and the glyphs drawn for each shape.

import { test, assertEqual } from "./harness.js";
import { FILE, PROJECT, fileIconSpec, glyph, isGlyph, labelSize, projectIconSpec, projectNeedsMarkers } from "/ui-kit/fileglyphs.js";
import { iconFrom } from "/ui-kit/dom.js";

const LANGS = { id: "langs", base: "/plugins/langs/" };
const C4 = { id: "c4", base: "/plugins/c4/" };

const FILE_ICONS = [
	{ plugin: LANGS, value: { extensions: [".ts"], label: "TS", color: "--hue-blue" } },
	{ plugin: LANGS, value: { extensions: [".d.ts"], label: "D", color: "--hue-blue" } },
	{ plugin: LANGS, value: { extensions: ["png"], icon: "$(file-media)", color: "--hue-green" } },
	{ plugin: LANGS, value: { fileNames: ["Dockerfile"], label: "DK", color: "--hue-cyan" } },
	{ plugin: LANGS, value: { extensions: [".toml"], label: "CFG" } },
	{ plugin: C4, value: { fileNames: ["nuke.toml"], label: "NK", color: "--hue-amber" } },
	{ plugin: C4, value: { extensions: [".cf"], label: "C4", color: "--hue-teal" } },
];

const PROJECT_ICONS = [
	{ plugin: C4, value: { languageId: "c4", label: "C4", color: "--hue-teal", presets: ["c4.nuke"] } },
	{ plugin: LANGS, value: { languageId: "rust", label: "RS", color: "--hue-orange", buildSystems: ["cargo"], markers: ["Cargo.toml"] } },
	{ plugin: LANGS, value: { languageId: "csharp", label: "C#", color: "--hue-violet", markers: ["*.csproj"] } },
];

test("file icons: a named file beats its extension, a longer extension a shorter", () => {
	assertEqual(fileIconSpec("src/app.ts", FILE_ICONS).label, "TS");
	assertEqual(fileIconSpec("types/index.d.ts", FILE_ICONS).label, "D", "the longer extension");
	assertEqual(fileIconSpec("Tools/nuke.toml", FILE_ICONS).label, "NK", "the file's own name");
	assertEqual(fileIconSpec("other.toml", FILE_ICONS).label, "CFG");
	assertEqual(fileIconSpec("DOCKERFILE", FILE_ICONS).label, "DK", "names match without case");
	assertEqual(fileIconSpec("x.cf", FILE_ICONS).base, "/plugins/c4/", "the plugin's folder comes along");
	assertEqual(fileIconSpec("notes.txt", FILE_ICONS), FILE, "nothing says: a plain page");
	assertEqual(fileIconSpec(".ts", FILE_ICONS), FILE, "a name that is only the extension is not a .ts file");
});

test("file icons: a later plugin wins a tie", () => {
	const both = FILE_ICONS.concat([{ plugin: C4, value: { extensions: [".ts"], label: "T4" } }]);
	assertEqual(fileIconSpec("a.ts", both).label, "T4");
});

test("file icons: a plugin's own picture is not drawn as a page", () => {
	const spec = fileIconSpec("logo.PNG", FILE_ICONS);
	assertEqual([isGlyph(spec), spec.icon, spec.color], [false, "$(file-media)", "--hue-green"]);
	const el = iconFrom(spec);
	assertEqual([el.tagName, el.style.color], ["SPAN", "var(--hue-green)"], "a codicon in the plugin's color");
});

test("project icons: language, then presets or build system, then marker files", () => {
	const project = (data) => ({ name: "P", data });
	assertEqual(projectIconSpec(project({ language: "c4" }), PROJECT_ICONS).label, "C4");
	assertEqual(projectIconSpec(project({ presets: ["c4.nuke"] }), PROJECT_ICONS).label, "C4", "by its preset");
	assertEqual(projectIconSpec(project({ buildSystem: { id: "cargo" } }), PROJECT_ICONS).label, "RS", "by its build system");
	assertEqual(projectIconSpec(project({}), PROJECT_ICONS, ["src", "cargo.toml"]).label, "RS", "by a marker file");
	assertEqual(projectIconSpec(project({}), PROJECT_ICONS, ["Game.csproj"]).label, "C#", "a marker by extension");
	assertEqual(projectIconSpec(project({ language: "zig" }), PROJECT_ICONS), PROJECT, "a language no plugin draws");
	assertEqual(projectIconSpec(null, PROJECT_ICONS).shape, "project");
	assertEqual([projectNeedsMarkers(project({}), PROJECT_ICONS), projectNeedsMarkers(project({ language: "c4" }), PROJECT_ICONS)], [true, false]);
});

test("glyphs: one simple shape each, in its hue", () => {
	const shapes = ["file", "folder", "folder-open", "project", "solution"].map((shape) => glyph({ shape }));
	assertEqual(shapes.map((svg) => svg.getAttribute("class")), ["ci fg fg-file", "ci fg fg-folder", "ci fg fg-folder-open", "ci fg fg-project", "ci fg fg-solution"]);
	assertEqual(shapes.map((svg) => svg.style.getPropertyValue("--fg-hue")), ["var(--icon-line)", "", "", "var(--icon-project)", "var(--icon-solution)"], "folders keep their own color");
	const hue = (svg) => svg.style.getPropertyValue("--fg-hue");
	const cpp = glyph({ shape: "file", label: "C++", color: "--hue-indigo" }, "sm");
	assertEqual([cpp.querySelector("text").textContent, cpp.children.length, hue(cpp), cpp.classList.contains("sm")], ["C++", 1, "var(--hue-indigo)", true], "a labelled file is its label alone");
	assertEqual(glyph({ shape: "file" }).querySelector("path").getAttribute("class"), "fg-page", "an unnamed file is a page");
	const project = glyph({ shape: "project", label: "C4", color: "--hue-teal" });
	assertEqual([project.querySelector("rect").getAttribute("class"), project.querySelector("text").textContent, hue(project)], ["fg-tile", "C4", "var(--hue-teal)"]);
	assertEqual(hue(glyph({ shape: "project", label: "X", color: "red" })), "var(--icon-project)", "a raw color is not taken");
});

test("glyphs: a label is as large as fits", () => {
	const file = { width: 15, most: 12.5 };
	assertEqual(labelSize("h", file), 12.5, "one letter at the most");
	assertEqual(labelSize("C4", file) > labelSize("C++", file), true, "longer labels get smaller");
	assertEqual(labelSize("MD", file) < labelSize("C4", file), true, "wide capitals take more room");
});
