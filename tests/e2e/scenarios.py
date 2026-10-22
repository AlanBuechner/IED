"""The shell's interactions, end to end (docs/15, docs/19): each scenario drives the page the way a person does and
screenshots the states that matter. Both servers run every scenario unless it names one."""

import json
import os
import time

from harness import check

SCENARIOS = []
DESKTOPS = ("desktop", "laptop")


def scenario(viewports=DESKTOPS, targets=("mock", "real")):
	def register(fn):
		SCENARIOS.append((fn.__name__, fn, viewports, targets))
		return fn
	return register


def tab_names(run, cls=".tab"):
	return run.texts(".dock-host:not([hidden]) .dock-group.well " + cls + " .name")


def show_output(page):
	"""Shows the Output window: auto-hidden by default, it slides out (docs/15 "Window layouts")."""
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.output' })")
	page.wait_for_selector("select[aria-label='Show output from']")


def drag(page, start, end, steps=12):
	page.mouse.move(*start)
	page.mouse.down()
	page.mouse.move(start[0] + 15, start[1] + 15, steps=3)
	page.mouse.move(*end, steps=steps)
	time.sleep(0.2)
	page.mouse.up()
	time.sleep(0.6)


# ---------------------------------------------------------------------------------------------------------------
# Desktop and tablet

@scenario(viewports=("desktop", "laptop", "tablet"))
def shell_chrome(run):
	page = run.open()
	check(page.is_visible(".sidebar .sb-ws"), "the workspace switcher")
	check(page.is_visible('.sb-act[data-activity="ied.editor"].on'), "the editor activity, shown")
	check(run.texts(".menubar .mi")[:3] == ["File", "Edit", "View"], "the menu bar in VS order: %s" % run.texts(".menubar .mi"))
	if run.viewport == "tablet":
		check(run.texts(".dock-host:not([hidden]) .ah-left .ah-tab") == ["Solution Explorer"], "a tablet starts with its tool windows auto-hidden: %s" % run.texts(".dock-host:not([hidden]) .ah-left .ah-tab"))
		page.click(".dock-host:not([hidden]) .ah-left .ah-tab:has-text('Solution Explorer')")
		page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .tree-row:has-text('README.md')")
	else:
		page.wait_for_selector(".tree-row:has-text('README.md')")
		check(run.texts(".dock-host:not([hidden]) .dock .panel-title") == ["Solution Explorer"], "Solution Explorer docked: %s" % run.texts(".dock-host:not([hidden]) .dock .panel-title"))
	check(run.texts(".dock-host:not([hidden]) .ah-right .ah-tab") == ["Output"], "Output auto-hidden on the right: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	check(run.texts(".dock-host:not([hidden]) .ah-bottom .ah-tab") == ["Error List", "Find in Files"], "Error List and Find in Files at the bottom: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	check(run.texts(".statusbar .conn-state") == ["Ready"], "the status bar says Ready")
	check(page.is_visible(".menubar .conn.live"), "the title says live")
	run.shot("loaded")


@scenario()
def preview_and_pinned_tabs(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab.preview:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('main.c')")
	page.click(".tree-row:has-text('main.c')")
	page.wait_for_selector(".tab.preview:has-text('main.c')")
	check(tab_names(run) == ["main.c"], "the preview tab was replaced: %s" % tab_names(run))
	page.dblclick(".tab:has-text('main.c')")
	page.wait_for_selector(".tab:not(.preview):has-text('main.c')")
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab.preview:has-text('README.md')")
	check(tab_names(run) == ["main.c", "README.md"], "a pinned tab stays: %s" % tab_names(run))
	run.shot("two-tabs")
	page.hover(".tab:has-text('main.c')")
	page.click(".tab:has-text('main.c') .x")
	page.wait_for_selector(".tab:has-text('main.c')", state="detached")
	check(tab_names(run) == ["README.md"], "closed")


@scenario()
def menus_and_palette(run):
	page = run.open()
	page.click("button.mi:has-text('View')")
	page.wait_for_selector(".menu")
	labels = run.texts(".menu .menu-item .label")
	for wanted in ("Command Palette...", "Solution Explorer", "Output", "Editor", "Notifications"):
		check(wanted in labels, "View has %s: %s" % (wanted, labels))
	check("Ctrl+Alt+O" in run.texts(".menu .menu-item .kbd"), "menu items show their keys")
	run.shot("view-menu")
	page.keyboard.press("ArrowDown")
	page.keyboard.press("Escape")
	page.wait_for_selector(".menu", state="detached")
	page.keyboard.press("Control+Shift+P")
	page.wait_for_selector(".palette-input")
	page.keyboard.type("keyboard")
	time.sleep(0.2)
	run.shot("palette")
	page.keyboard.press("Enter")
	page.wait_for_selector(".kb-row")
	rows = run.texts(".kb-row .kb-label")
	check("View: Command Palette..." in rows and "View: Output" in rows, "Settings > Environment > Keyboard: %s" % rows[:20])
	run.shot("shortcuts")


@scenario()
def docking_persists(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab:has-text('README.md')")
	page.keyboard.press("Control+Shift+P")
	page.keyboard.type("about")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab:has-text('About IED')")
	tab = page.locator(".tab:has-text('About IED')").bounding_box()
	well = page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()
	drag(page, (tab["x"] + 30, tab["y"] + 12), (well["x"] + well["width"] - 20, well["y"] + well["height"] / 2))
	check(len(page.query_selector_all(".dock-host:not([hidden]) .dock-group.well")) == 2, "the tab made a second document group")
	run.shot("split")
	time.sleep(0.8)
	run.reload()
	check(len(page.query_selector_all(".dock-host:not([hidden]) .dock-group.well")) == 2, "the split is kept by the server")
	check(page.is_visible(".tab:has-text('About IED')"), "with its tab")
	run.shot("reloaded")


def read_file(run, path):
	return run.page.evaluate("(p) => ied_shell.client.request('fs.read', { wsId: ied_shell.wsId, path: p }).then(r => r.text, () => null)", path)


@scenario(viewports=("desktop",))
def tabs_like_a_browser(run):
	"""Tabs as in a web browser (docs/15 "Docking"): a tab dragged along its strip slides into a new place, kept by the
	server; "+" opens Untitled-1.txt, kept as it is typed (.ied/local/untitled/) and saved into the workspace with Ctrl+S;
	in the Chat activity "+" opens a new chat."""
	page = run.open()
	for path in ("README.md", "src/main.c"):
		page.evaluate("(p) => ied_shell.commands.run('ied.editor.goTo', { path: p })", path)
		page.wait_for_selector(".dock-host:not([hidden]) .tab:has-text('%s')" % path.split("/")[-1])
	check(tab_names(run) == ["README.md", "main.c"], "opened in order: %s" % tab_names(run))
	tabs = ".dock-host:not([hidden]) .dock-group.well .tabs .tab"
	first = page.locator(tabs).first.bounding_box()
	last = page.locator(tabs).last.bounding_box()
	drag(page, (first["x"] + 20, first["y"] + 8), (last["x"] + last["width"] - 4, first["y"] + 8))
	check(tab_names(run) == ["main.c", "README.md"], "dragged past main.c: %s" % tab_names(run))
	time.sleep(0.8)
	run.reload()
	check(tab_names(run) == ["main.c", "README.md"], "the order is kept by the server: %s" % tab_names(run))
	page.click(".dock-host:not([hidden]) .tabs-new")
	page.wait_for_selector(".tab.on:has-text('Untitled-1.txt')")
	view = ".view[data-ref='doc:.ied/local/untitled/Untitled-1.txt']"
	page.wait_for_selector(view + " .monaco-editor .view-lines", timeout=30000)
	page.click(view + " .monaco-editor .view-lines")
	page.keyboard.type("remember the milk")
	page.wait_for_function("() => ied_shell.client.request('fs.read', { wsId: ied_shell.wsId, path: '.ied/local/untitled/Untitled-1.txt' }).then(r => r.text === 'remember the milk', () => false)", polling=300, timeout=10000)
	run.shot("untitled")
	page.keyboard.press("Control+s")
	page.wait_for_selector(".dialog input")
	page.fill(".dialog input", "notes/todo.txt")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab.on:has-text('todo.txt')", timeout=10000)
	page.wait_for_function("() => ied_shell.client.request('fs.read', { wsId: ied_shell.wsId, path: 'notes/todo.txt' }).then(r => r.text === 'remember the milk', () => false)", polling=300, timeout=10000)
	check(read_file(run, ".ied/local/untitled/Untitled-1.txt") is None, "the untitled file moved into the workspace")
	run.show_activity("ied.chat")
	page.click(".dock-host:not([hidden]) .tabs-new")
	page.wait_for_selector(".tab.on[data-ref='chat:new']")
	run.shot("new-chat")


@scenario()
def tab_context_menu(run):
	page = run.open()
	for name in ("README.md", ".gitignore" if run.target.kind == "mock" else "docs"):
		page.click(".tree-row:has-text('%s')" % name)
	if run.target.kind == "real":
		page.wait_for_selector(".tree-row:has-text('notes.md')")
		page.dblclick(".tree-row:has-text('notes.md')")
	else:
		page.dblclick(".tree-row:has-text('.gitignore')")
	page.dblclick(".tree-row:has-text('README.md')")
	time.sleep(0.4)
	before = tab_names(run)
	check(len(before) == 2, "two tabs open: %s" % before)
	page.click(".tab:has-text('README.md')", button="right")
	page.wait_for_selector(".menu")
	labels = run.texts(".menu .menu-item .label")
	check(labels[:3] == ["Close", "Close All But This", "Close All"], "the tab menu: %s" % labels)
	run.shot("menu")
	page.click(".menu .menu-item:has-text('Close All But This')")
	page.wait_for_function("document.querySelectorAll('.dock-host:not([hidden]) .dock-group.well .tab').length === 1")
	check(tab_names(run) == ["README.md"], "only README.md is left: %s" % tab_names(run))


@scenario(viewports=("desktop",))
def editor_context_menu(run):
	"""The editor's own context menu (Monaco draws it in a shadow root) has the theme's background, not bare text over
	the code: the theme's colors reach the shadow root's host."""
	page = run.open()
	page.dblclick(".tree-row:has-text('README.md')")
	page.wait_for_selector(".dock-host:not([hidden]) .dock-body .monaco-editor .view-lines", timeout=30000)
	page.click(".dock-host:not([hidden]) .dock-body .monaco-editor .view-lines", button="right")
	menu = "document.querySelector('.shadow-root-host') && document.querySelector('.shadow-root-host').shadowRoot.querySelector('.monaco-menu-container .monaco-scrollable-element')"
	page.wait_for_function("!!(" + menu + ")", timeout=10000)
	background = page.evaluate("getComputedStyle(" + menu + ").backgroundColor")
	check(background not in ("", "transparent", "rgba(0, 0, 0, 0)"), "the menu has a background: %s" % background)
	run.shot("menu")
	page.keyboard.press("Escape")


@scenario()
def tool_windows_close_and_come_back(run):
	"""A closed tool window comes back where the layout's default puts it: Solution Explorer docked and focused, Output
	auto-hidden and slid out."""
	page = run.open()
	page.click(".dock-host:not([hidden]) .dock-group.tool:has(.panel-title:has-text('Solution Explorer')) .panel-title button[aria-label='Close Solution Explorer']")
	page.wait_for_selector(".panel-title:has-text('Solution Explorer')", state="detached")
	page.click(".dock-host:not([hidden]) .ah-right .ah-tab:has-text('Output')")
	page.click(".dock-host:not([hidden]) .ah-flyout .panel-title button[aria-label='Close Output']")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-tab:has-text('Output')", state="detached")
	run.shot("closed")
	page.click(".dock-host:not([hidden]) .dock-group.well")
	page.keyboard.press("Control+Alt+O")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .panel-title:has-text('Output')")
	check(run.texts(".dock-host:not([hidden]) .ah-right .ah-tab") == ["Output"], "back on the right edge: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	page.keyboard.press("Control+Alt+L")
	page.wait_for_selector(".dock-host:not([hidden]) .dock .panel-title:has-text('Solution Explorer')")
	page.wait_for_function("document.activeElement.closest('.view') && document.activeElement.closest('.view').dataset.ref === 'ied.explorer'", timeout=5000)
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout", state="detached")
	run.shot("back")


@scenario()
def tool_windows_auto_hide(run):
	"""VS auto-hide, one tool window at a time (docs/15 "Auto-hide"): a strip tab slides its window out over the
	documents until a click elsewhere or Escape; Dock puts it into the layout; the pin on a docked window's title hides
	that window alone, its group's others staying docked; a group with nothing docked gives its room to the documents;
	the server keeps it all; Auto Hide All, and Reset Window Layout back to the default."""
	page = run.open()
	group = ".dock-host:not([hidden]) .dock .dock-group.tool:has(.panel-title:has-text('%s'))"
	page.wait_for_selector(".dock-host:not([hidden]) .ah-right .ah-tab:has-text('Output')")
	well = page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()
	page.click(".dock-host:not([hidden]) .ah-right .ah-tab:has-text('Output')")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .panel-title:has-text('Output')")
	time.sleep(0.3)
	flyout = page.locator(".dock-host:not([hidden]) .ah-flyout").bounding_box()
	check(flyout["x"] > well["x"] + 100 and flyout["x"] + flyout["width"] >= well["x"] + well["width"] - 1, "from the right, over the documents: %s" % flyout)
	check(abs(page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()["width"] - well["width"]) < 2, "over them, not beside them")
	run.shot("slid-out")
	page.click(".dock-host:not([hidden]) .dock-group.well .dock-body", position={"x": 40, "y": 40})
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout", state="detached")
	for name in ("Error List", "Find in Files"):
		page.click(".dock-host:not([hidden]) .ah-bottom .ah-tab:has-text('%s')" % name)
		page.click(".dock-host:not([hidden]) .ah-flyout .panel-title button[aria-label='Dock %s']" % name)
		page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout", state="detached")
	page.wait_for_function("[...document.querySelectorAll('.dock-host:not([hidden]) .dock .tw-tab')].map((t) => t.textContent).join() === 'Error List,Find in Files'")
	check(page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()["height"] < well["height"] - 100, "docked, they take room")
	page.click(group % "Find in Files" + " .panel-title button[aria-label='Auto Hide Find in Files']")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-bottom .ah-tab:has-text('Find in Files')")
	page.wait_for_selector(group % "Error List")
	check(run.texts(".dock-host:not([hidden]) .ah-tab") == ["Find in Files", "Output"], "only Find in Files is hidden: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	run.shot("one-hidden")
	page.click(group % "Solution Explorer" + " .panel-title button[aria-label='Auto Hide Solution Explorer']")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-left .ah-tab:has-text('Solution Explorer')")
	check(page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()["width"] > well["width"] + 100, "the documents took its room")
	page.click(".dock-host:not([hidden]) .ah-left .ah-tab:has-text('Solution Explorer')")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .tree-row:has-text('README.md')")
	page.keyboard.press("Escape")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout", state="detached")
	time.sleep(0.8)
	run.reload()
	page.wait_for_selector(".dock-host:not([hidden]) .ah-left .ah-tab:has-text('Solution Explorer')")
	check(run.texts(".dock-host:not([hidden]) .ah-tab") == ["Solution Explorer", "Find in Files", "Output"], "the server kept it: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	page.click(".dock-host:not([hidden]) .dock-group.well .dock-body", position={"x": 40, "y": 40})
	page.keyboard.press("Control+Alt+O")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .panel-title:has-text('Output')")
	page.click(".dock-host:not([hidden]) .ah-flyout .panel-title button[aria-label='Dock Output']")
	page.wait_for_selector(group % "Output")
	page.click("button.mi:has-text('Window')")
	page.click(".menu .menu-item:has-text('Auto Hide All')")
	page.wait_for_selector(".dock-host:not([hidden]) .ah-right .ah-tab:has-text('Output')")
	check(run.texts(".dock-host:not([hidden]) .ah-tab") == ["Solution Explorer", "Error List", "Find in Files", "Output"], "every tool window: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
	run.shot("all-hidden")
	page.click("button.mi:has-text('Window')")
	page.click(".menu .menu-item:has-text('Reset Window Layout')")
	page.wait_for_selector(group % "Solution Explorer")
	check(run.texts(".dock-host:not([hidden]) .ah-tab") == ["Error List", "Find in Files", "Output"], "the default again: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))


@scenario(targets=("mock",))
def window_layouts_for_editing_and_debugging(run):
	"""VS's editing and debugging layouts (docs/15 "Window layouts"): a debug session shows the debugging layout - Output
	docked at the bottom beside the variables, Diagnostic Tools on the right - with the documents as they were; a change made while debugging is there
	at the next session; stopping brings the editing layout back."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 5}]})
	try:
		open_main_c(run, page)
		page.evaluate("ied_shell.commands.run('ied.debug.start')")
		page.wait_for_selector(".statusbar[data-state='debugging']")
		page.wait_for_selector(".dock-host:not([hidden]) .dock .panel-title:has-text('Locals')")
		titles = run.texts(".dock-host:not([hidden]) .dock .panel-title")
		check(titles == ["Solution Explorer", "Output", "Locals", "Diagnostic Tools"], "the debugging layout: %s" % titles)
		check(run.texts(".dock-host:not([hidden]) .dock .tw-tab") == ["Output", "Error List", "Call Stack", "Breakpoints", "Locals", "Watch", "Threads"], "its groups: %s" % run.texts(".dock-host:not([hidden]) .dock .tw-tab"))
		check(tab_names(run) == ["main.c"], "the document came along: %s" % tab_names(run))
		check(run.texts(".dock-host:not([hidden]) .ah-tab") == ["Find in Files"], "Find in Files hidden: %s" % run.texts(".dock-host:not([hidden]) .ah-tab"))
		run.shot("debugging")
		page.click(".dock-host:not([hidden]) .dock .dock-group.tool:has(.panel-title:has-text('Locals')) .panel-title button[aria-label='Auto Hide Locals']")
		page.wait_for_selector(".dock-host:not([hidden]) .ah-bottom .ah-tab:has-text('Locals')")
		page.evaluate("ied_shell.commands.run('ied.debug.stop')")
		page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
		page.wait_for_selector(".dock-host:not([hidden]) .ah-right .ah-tab:has-text('Output')")
		check(run.texts(".dock-host:not([hidden]) .dock .panel-title") == ["Solution Explorer"], "editing again: %s" % run.texts(".dock-host:not([hidden]) .dock .panel-title"))
		check(tab_names(run) == ["main.c"], "with the document: %s" % tab_names(run))
		page.evaluate("ied_shell.commands.run('ied.debug.start')")
		page.wait_for_selector(".statusbar[data-state='debugging']")
		page.wait_for_selector(".dock-host:not([hidden]) .ah-bottom .ah-tab:has-text('Locals')")
		check(page.query_selector(".dock-host:not([hidden]) .dock .panel-title:has-text('Locals')") is None, "the debugging layout kept the change")
		page.evaluate("ied_shell.commands.run('ied.debug.stop')")
		page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
	finally:
		run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})


@scenario()
def splitter_resizes(run):
	page = run.open()
	sash = page.locator(".dock-host:not([hidden]) .dock > .dock-split > .dock-sash").first.bounding_box()
	before = page.locator(".dock-host:not([hidden]) .dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	drag(page, (sash["x"] + sash["width"] / 2, sash["y"] + 200), (sash["x"] + 120, sash["y"] + 200), steps=6)
	after = page.locator(".dock-host:not([hidden]) .dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	check(abs(after - before - 120) < 6, "Solution Explorer grew by the drag: %d -> %d" % (before, after))
	time.sleep(0.8)
	run.reload()
	kept = page.locator(".dock-host:not([hidden]) .dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	check(abs(kept - after) < 3, "the size is kept: %d vs %d" % (kept, after))


@scenario(viewports=("desktop", "laptop", "phone"))
def notifications(run):
	page = run.open()
	run.request("server.notify", {"text": "Build finished: 0 errors", "level": "info"})
	page.wait_for_selector(".toast:has-text('Build finished')")
	selector = ".phone-head .badge" if run.phone else ".sidebar .badge"
	page.wait_for_selector(selector)
	check(run.texts(selector) == ["1"], "the bell counts one: %s" % run.texts(selector))
	run.shot("toast")
	page.click(".phone-head button[aria-label^='Notifications']" if run.phone else ".sidebar .sb-bell")
	page.wait_for_selector(".notification-center .nc-item:has-text('Build finished')")
	run.shot("center")
	page.click(".notification-center .nc-item button")
	page.wait_for_selector(".notification-center .nc-empty")
	page.wait_for_selector(selector, state="detached")


@scenario()
def output_streams(run):
	page = run.open()
	show_output(page)
	if run.target.kind == "mock":
		page.select_option("select[aria-label='Show output from']", "build")
		run.request("task.run", {"wsId": "w1", "task": "build"})
		run.wait_text(".output-text .line", "Build succeeded", timeout=20000)
		check(page.query_selector(".output-text .a-yellow, .output-text .a-byellow, .output-text .a-green, .output-text .a-bgreen") is not None, "ANSI colors became runs")
	else:
		page.select_option("select[aria-label='Show output from']", "ied")
		run.wait_text(".output-text .line", "opened w1")
	run.shot("output")


@scenario()
def resume_after_server_restart(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab:has-text('README.md')")
	time.sleep(0.6)
	epoch = run.request("server.info")["sessionEpoch"]
	page.evaluate("ied_shell.client.request('testhooks.restart', {}).catch(() => {})")
	page.wait_for_function("ied_shell.client.state !== 'live'", timeout=10000)
	run.shot("reconnecting")
	page.wait_for_function("ied_shell.client.state === 'live'", timeout=20000)
	page.wait_for_selector(".banner[data-banner='connection']", state="detached")
	check(run.request("server.info")["sessionEpoch"] != epoch, "a new epoch")
	check(page.is_visible(".tab:has-text('README.md')"), "the tab is still there")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	run.shot("resumed")


@scenario(targets=("mock",))
def reopen_mid_stream(run):
	page = run.open()
	show_output(page)
	page.select_option("select[aria-label='Show output from']", "build")
	run.request("task.run", {"wsId": "w1", "task": "flood", "params": {"lines": 3000}})
	run.wait_text(".output-text .line", "line")
	page.context.close()
	time.sleep(1.0)
	page = run.open(fresh=False)
	show_output(page)
	page.select_option("select[aria-label='Show output from']", "build")
	page.wait_for_function("[...document.querySelectorAll('.output-text .line')].some(l => /3000/.test(l.textContent))", timeout=30000)
	run.shot("reopened")


@scenario()
def trust_banner(run):
	page = run.open()
	run.request("ws.setTrust", {"wsId": page.evaluate("ied_shell.wsId"), "trusted": False})
	page.wait_for_selector(".banner[data-banner='trust']")
	run.shot("untrusted")
	page.click(".banner[data-banner='trust'] button:has-text('Trust Workspace')")
	page.wait_for_selector(".banner[data-banner='trust']", state="detached")


FANCY_PLUGIN = {
	"manifestVersion": 1, "id": "fancy", "name": "Fancy", "version": "1.0.0", "ied": {"api": "^1"}, "runtime": {"kind": "declarative"},
	"contributes": {"languages": [{"id": "fancy", "extensions": [".fy"], "lineComment": "//", "grammar": "fancy.json"}]},
}
FANCY_GRAMMAR = {"tokenizer": {"root": [[r"\bfancy\b", "keyword"], [r"//.*$", "comment"]]}}


@scenario(viewports=("desktop", "phone"), targets=("real",))
def a_language_trust_brings_reaches_open_tabs(run):
	"""A tab open while the workspace is untrusted takes the language its project plugin brings once the owner trusts
	it, without a reload: the server tells the document (doc.language, docs/10) and the editor registers the language
	with Monaco when the contributions are read again (contributions.on, docs/02)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	trusted = page.evaluate("ied_shell.workspace.trusted")
	path = "notes-%s.fy" % run.viewport
	model = "ied_shell.views.entries.get('doc:' + path).instance.model"
	try:
		run.request("ws.setTrust", {"wsId": ws, "trusted": False})
		page.wait_for_selector(".banner[data-banner='trust']")
		run.request("fs.write", {"wsId": ws, "path": ".ied/plugins/fancy/ied-plugin.json", "text": json.dumps(FANCY_PLUGIN)})
		run.request("fs.write", {"wsId": ws, "path": ".ied/plugins/fancy/fancy.json", "text": json.dumps(FANCY_GRAMMAR)})
		run.request("fs.write", {"wsId": ws, "path": path, "text": "fancy words // and a comment\n"})
		page.evaluate("(path) => ied_shell.openTab('doc:' + path)", path)
		page.wait_for_function("(path) => { const e = ied_shell.views.entries.get('doc:' + path); return !!(e && e.instance && e.instance.model); }", arg=path, timeout=20000)
		check(page.evaluate("(path) => " + model + ".getLanguageId()", path) == "plaintext", "no plugin knows .fy while untrusted")
		run.shot("untrusted")
		page.click(".banner[data-banner='trust'] button:has-text('Trust Workspace')")
		page.wait_for_function("(path) => " + model + ".getLanguageId() === 'fancy'", arg=path, timeout=15000)
		if run.viewport != "phone":
			run.wait_text(".statusbar [data-item='ied.editor.status']", "fancy")
		run.shot("trusted")
	finally:
		run.request("ws.setTrust", {"wsId": ws, "trusted": trusted})
		run.request("fs.delete", {"wsId": ws, "path": ".ied/plugins/fancy", "recursive": True})
		run.request("fs.delete", {"wsId": ws, "path": path})


@scenario()
def plugins_page(run):
	page = run.open()
	page.click("button.mi:has-text('Extensions')")
	page.click(".menu .menu-item:has-text('Manage Plugins')")
	page.wait_for_selector(".plugin-card[data-plugin='ied.editor']")
	check("loaded" in run.texts(".plugin-card[data-plugin='ied.editor'] .chip"), "the editor plugin is loaded")
	run.shot("plugins")


@scenario(viewports=("desktop",))
def disable_and_enable_a_plugin(run):
	page = run.open()
	try:
		page.click("button.mi:has-text('Extensions')")
		page.click(".menu .menu-item:has-text('Manage Plugins')")
		page.wait_for_selector(".plugin-card[data-plugin='ied.editor'] button:has-text('Disable')")
		page.click(".plugin-card[data-plugin='ied.editor'] button:has-text('Disable')")
		page.wait_for_selector('.sb-act[data-activity="ied.editor"]', state="detached")
		# Another dock host keeps documents a home (the Profiler activity is one); with none left the shell adds Home.
		page.wait_for_selector('.sb-act[data-activity="shell.home"], .sb-act[data-activity="ied.profiler"]')
		page.wait_for_selector('.sb-act[data-activity="ied.chat"].on')
		page.wait_for_selector(".panel-title:has-text('Solution Explorer')", state="hidden")
		run.shot("disabled")
		page.click("button.mi:has-text('Extensions')")
		page.click(".menu .menu-item:has-text('Manage Plugins')")
		page.wait_for_selector(".plugin-card[data-plugin='ied.editor'] button:has-text('Enable')")
		page.click(".plugin-card[data-plugin='ied.editor'] button:has-text('Enable')")
		page.wait_for_selector('.sb-act[data-activity="ied.editor"]')
		page.wait_for_selector('.sb-act[data-activity="shell.home"]', state="detached")
		page.click('.sb-act[data-activity="ied.editor"]')
		page.wait_for_selector(".panel-title:has-text('Solution Explorer')")
		page.wait_for_selector(".tree-row:has-text('README.md')")
		run.shot("enabled")
	finally:
		# A failure part-way must not leave the editor disabled for the scenarios after this one.
		run.request("plugins.setEnabled", {"id": "ied.editor", "enabled": True})


# ---------------------------------------------------------------------------------------------------------------
# Solution Explorer file operations

def exists(run, path):
	try:
		run.request("fs.read", {"wsId": run.page.evaluate("ied_shell.wsId"), "path": path})
		return True
	except Exception:
		return False


@scenario()
def explorer_file_operations(run):
	page = run.open()
	name = "made-" + run.viewport
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New File...')")
	page.wait_for_selector(".tree-edit")
	run.shot("naming")
	page.keyboard.type(name + ".c")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s.c')" % name)
	page.wait_for_selector(".tab:has-text('%s.c')" % name)
	check(exists(run, "src/%s.c" % name), "the new file is on disk")
	page.click(".tree-row:has-text('%s.c')" % name)
	page.keyboard.press("F2")
	page.wait_for_selector(".tree-edit")
	check(page.evaluate("(() => { const i = document.querySelector('.tree-edit'); return [i.selectionStart, i.selectionEnd]; })()") == [0, len(name)], "the name is selected, not its extension")
	page.keyboard.type(name + "-renamed")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name)
	check(exists(run, "src/%s-renamed.c" % name) and not exists(run, "src/%s.c" % name), "renamed on disk")
	page.click(".tree-row:has-text('%s-renamed.c')" % name)
	page.keyboard.press("Delete")
	page.wait_for_selector(".popup.dialog")
	run.shot("confirm-delete")
	page.keyboard.press("Escape")
	page.wait_for_selector(".popup.dialog", state="detached")
	check(exists(run, "src/%s-renamed.c" % name), "Escape keeps the file")
	page.keyboard.press("Delete")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name, state="detached")
	check(not exists(run, "src/%s-renamed.c" % name), "deleted on disk")
	page.click(".toast button:has-text('Undo')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name)
	check(exists(run, "src/%s-renamed.c" % name), "Undo brought it back from the trash")
	page.click(".tree-row:has-text('%s-renamed.c')" % name)
	page.keyboard.press("Delete")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name, state="detached")
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New Folder')")
	page.keyboard.type(name + "-dir")
	page.keyboard.press("Escape")
	page.wait_for_selector(".tree-edit", state="detached")
	check(not page.is_visible(".tree-row:has-text('%s-dir')" % name), "Escape makes nothing")
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New Folder')")
	page.keyboard.type(name + "-dir")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-dir')" % name)
	run.shot("folder")
	page.click(".tree-row:has-text('%s-dir')" % name)
	page.keyboard.press("F2")
	page.keyboard.type(name + "-moved")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-moved')" % name)
	page.click(".tree-row:has-text('%s-moved')" % name)
	page.keyboard.press("Delete")
	check("everything in it" in page.text_content(".popup.dialog"), "a folder's delete says it takes its contents")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-moved')" % name, state="detached")


@scenario()
def find_in_files(run):
	"""Ctrl+Shift+F, a query, the results by file, and a match opened at its place (docs/10 fs.search)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	token = "findme_%s_%d" % (run.viewport, int(time.time()))
	run.request("fs.write", {"wsId": ws, "path": "src/find-%s.c" % run.viewport, "text": "int other;\n\tint %s = 1;\n" % token})
	page.click(".tree-row:has-text('README.md')")
	page.keyboard.press("Control+Shift+F")
	page.wait_for_selector(".fif-query")
	page.wait_for_function("document.activeElement && document.activeElement.classList.contains('fif-query')")
	page.keyboard.type(token)
	page.keyboard.press("Enter")
	page.wait_for_selector(".fif-results .tree-row:has-text('find-%s.c')" % run.viewport)
	check("1 match in 1 file" in page.text_content(".fif-status"), "the count: %s" % page.text_content(".fif-status"))
	run.shot("results")
	page.dblclick(".fif-results .tree-row:has-text('%s')" % token)
	page.wait_for_selector(".tab:has-text('find-%s.c')" % run.viewport)
	page.wait_for_function("document.querySelector('.statusbar') && document.querySelector('.statusbar').textContent.includes('Ln 2, Col 6')", timeout=20000)
	run.request("fs.delete", {"wsId": ws, "path": "src/find-%s.c" % run.viewport})


@scenario()
def go_to_all(run):
	"""Ctrl+, finds a file by a few letters of its name and opens it (docs/10 fs.files); Escape closes the picker."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	name = "gotoall%s%d.c" % (run.viewport, int(time.time()))
	run.request("fs.write", {"wsId": ws, "path": "src/deep/%s" % name, "text": "int x;\n"})
	page.click(".tree-row:has-text('README.md')")
	page.keyboard.press("Control+Comma")
	page.wait_for_selector(".picker .palette-input")
	page.keyboard.press("Escape")
	page.wait_for_selector(".picker", state="detached")
	page.keyboard.press("Control+Comma")
	page.wait_for_selector(".picker .palette-input")
	page.keyboard.type("gtall" + name[7:-2])
	page.wait_for_selector(".picker .menu-item.sel:has-text('%s')" % name)
	check("src/deep" in page.text_content(".picker .menu-item.sel"), "the folder beside the name")
	run.shot("picker")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab:has-text('%s')" % name)
	check(page.query_selector(".picker") is None, "the picker closes on a pick")
	run.request("fs.delete", {"wsId": ws, "path": "src/deep/%s" % name})


def active_tab(page):
	return page.evaluate("ied_shell.context.get('activeTab')")


@scenario(viewports=("desktop",))
def navigate_back_and_forward(run):
	"""Ctrl+- goes back to where the cursor was before a jump to another document, Ctrl+Shift+- forward again."""
	page = run.open()
	open_main_c(run, page)
	page.click(".monaco-editor .view-lines")
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'README.md', line: 0, col: 0 })")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:README.md'", timeout=20000)
	page.wait_for_function("ied_shell.views.entries.get('doc:README.md').instance.editor.hasTextFocus()", timeout=20000)
	page.keyboard.press("Control+Minus")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:src/main.c'", timeout=20000)
	page.keyboard.press("Control+Shift+Minus")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:README.md'", timeout=20000)
	check(active_tab(page) == "doc:README.md", "forward again")


@scenario(viewports=("desktop",))
def toggle_header_and_code(run):
	"""Ctrl+K, Ctrl+O between a C file and its header (the languages plugin's headerSourcePairs)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	for name in ("pair.c", "pair.h"):
		run.request("fs.write", {"wsId": ws, "path": "src/" + name, "text": "int pair;\n"})
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'src/pair.c', line: 0 })")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/pair.c') && ied_shell.views.entries.get('doc:src/pair.c').instance.editor.hasTextFocus()", timeout=20000)
	page.keyboard.press("Control+K")
	page.keyboard.press("Control+O")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:src/pair.h'", timeout=20000)
	header = "ied_shell.views.entries.get('doc:src/pair.h').instance"
	page.wait_for_function("%s && %s.model && %s.sync.holdsLease && %s.editor.hasTextFocus()" % (header, header, header, header), timeout=20000)
	page.keyboard.press("Control+K")
	page.keyboard.press("Control+C")
	page.wait_for_function("%s.model.getValue().startsWith('//')" % header, timeout=10000)
	check(True, "Ctrl+K, Ctrl+C still comments the line")
	for name in ("pair.c", "pair.h"):
		run.request("fs.delete", {"wsId": ws, "path": "src/" + name})


@scenario(viewports=("desktop",))
def renamed_document_follows(run):
	"""A file renamed while it is open: its tab moves to the new name with the unsaved text (docs/10)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("fs.write", {"wsId": ws, "path": "src/follow.c", "text": "int x;\n", "expectedRevision": 0})
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('follow.c')")
	page.dblclick(".tree-row:has-text('follow.c')")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/follow.c') && ied_shell.views.entries.get('doc:src/follow.c').instance && ied_shell.views.entries.get('doc:src/follow.c').instance.model", timeout=30000)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+End")
	page.keyboard.type("int y;")
	page.wait_for_selector(".tab:has-text('follow.c') .dirty")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/follow.c').instance.sync.pending === 0", timeout=10000)
	run.request("fs.rename", {"wsId": ws, "from": "src/follow.c", "to": "src/followed.c"})
	page.wait_for_selector(".tab:has-text('followed.c')")
	page.wait_for_selector(".tab:has-text('follow.c')", state="detached")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/followed.c') && ied_shell.views.entries.get('doc:src/followed.c').instance && ied_shell.views.entries.get('doc:src/followed.c').instance.model", timeout=30000)
	text = page.evaluate("ied_shell.views.entries.get('doc:src/followed.c').instance.model.getValue()")
	check(text == "int x;\nint y;", "the unsaved text came along: %r" % text)
	page.wait_for_selector(".tab:has-text('followed.c') .dirty")
	run.shot("followed")
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('followed.c') .dirty", state="detached")
	check(run.request("fs.read", {"wsId": ws, "path": "src/followed.c"})["text"] == "int x;\nint y;", "saved under the new name")
	check(not exists(run, "src/follow.c"), "nothing written back to the old name")
	run.request("fs.delete", {"wsId": ws, "path": "src/followed.c"})


# ---------------------------------------------------------------------------------------------------------------
# Editing (Monaco on server documents)

def open_main_c(run, page):
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('main.c')")
	page.dblclick(".tree-row:has-text('main.c')")
	page.wait_for_selector(".monaco-editor .view-lines", timeout=30000)
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c') && ied_shell.views.entries.get('doc:src/main.c').instance && ied_shell.views.entries.get('doc:src/main.c').instance.sync.version > 0", timeout=20000)


def take_over(page):
	"""Edit Here, when a window of an earlier scenario still holds the lease (it lasts until two minutes idle, docs/11)."""
	if page.is_visible(".ed-bar button:has-text('Edit Here')"):
		page.click(".ed-bar button:has-text('Edit Here')")
		page.wait_for_selector(".ed-bar", state="hidden")


def editor_text(page):
	return page.evaluate("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue()")


def server_text(run):
	return run.request("fs.read", {"wsId": run.page.evaluate("ied_shell.wsId"), "path": "src/main.c"})["text"]


def settled(page):
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.sync.pending === 0", timeout=10000)


@scenario()
def edit_and_save(run):
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("// edited in IED\n")
	page.wait_for_selector(".tab:has-text('main.c') .dirty")
	run.shot("typed")
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(server_text(run).startswith("// edited in IED\n"), "saved to disk: %r" % server_text(run)[:40])
	check("Ln 2, Col 1" in run.texts(".statusbar [data-item='ied.editor.status']")[0], "the status bar has the cursor: %s" % run.texts(".statusbar .st"))
	run.reload()
	open_main_c(run, page)
	take_over(page)
	check(editor_text(page).startswith("// edited in IED\n"), "the saved text after a reload")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	for _ in range(len("// edited in IED\n")):
		page.keyboard.press("Delete")
	settled(page)
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(not server_text(run).startswith("// edited"), "the file is as it was")


@scenario(viewports=("desktop",))
def undo_to_saved_is_clean(run):
	"""Typing marks the tab unsaved; undoing back to the file's text clears the mark (docs/10 "dirty")."""
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	before = server_text(run)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("xy")
	page.wait_for_selector(".tab:has-text('main.c') .dirty")
	page.keyboard.press("Control+Z")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(editor_text(page) == before and server_text(run) == before, "the text is the file's, which was not written")


@scenario(viewports=("desktop",))
def track_changes_margin(run):
	"""A typed line is marked unsaved, then saved once saved; undone and saved again, the file is as it was and
	nothing is marked (docs/10 doc.baseline)."""
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	before = editor_text(page)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("// margin\n")
	page.wait_for_selector(".monaco-editor .tc-unsaved")
	run.shot("unsaved")
	page.keyboard.press("Control+S")
	# Earlier scenarios may have saved main.c already, so saved marks alone do not say this save landed.
	page.wait_for_selector(".monaco-editor .tc-unsaved", state="detached", timeout=10000)
	check(page.query_selector(".monaco-editor .tc-saved") is not None, "saved: the edited line is marked saved")
	run.shot("saved")
	for _ in range(20):
		if editor_text(page) == before:
			break
		page.keyboard.press("Control+Z")
	settled(page)
	page.keyboard.press("Control+S")
	page.wait_for_selector(".monaco-editor .tc-saved", state="detached")
	check(page.query_selector(".monaco-editor .tc-unsaved") is None and server_text(run) == before, "the file is as it was")


@scenario(viewports=("desktop",))
def file_outside_the_workspace(run):
	"""A file a language server named outside the workspace opens read-only at its place, its tab named by the file,
	the bar saying where it is (docs/10 "Files outside the workspace")."""
	page = run.open()
	name = "outside%d.h" % int(time.time())
	uri = run.request("testhooks.external", {"name": name, "text": "int outside;\n"})["uri"]
	page.evaluate("(uri) => ied_shell.commands.run('ied.editor.goTo', { path: uri, line: 0, col: 4 })", uri)
	page.wait_for_selector(".tab:has-text('%s')" % name)
	page.wait_for_selector(".ed-bar:has-text('outside the workspace')")
	model = "ied_shell.views.entries.get('doc:' + uri).instance.model"
	page.wait_for_function("(uri) => { const e = ied_shell.views.entries.get('doc:' + uri); return !!(e && e.instance && e.instance.model); }", arg=uri, timeout=20000)
	run.shot("external")
	page.keyboard.type("x")
	check(page.evaluate("(uri) => " + model + ".getValue()", uri) == "int outside;\n", "read-only: typing changes nothing")
	check("Ln 1, Col 5" in run.texts(".statusbar [data-item='ied.editor.status']")[0], "at its place: %s" % run.texts(".statusbar .st"))


@scenario(viewports=("desktop", "phone"))
def markdown_preview_ticks_a_task(run):
	"""The preview of a Markdown file shows its tasks, tables and code; ticking a box writes "[x]" into the document
	(J10, F-EDT-91/92)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/tasks-%s.md" % run.viewport
	run.request("fs.write", {"wsId": ws, "path": path, "text": "# Tasks\n\n- [ ] write the preview\n- [x] test it\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```c\nint x;\n```\n"})
	page.evaluate("(path) => ied_shell.commands.run('ied.markdown.preview', { path })", path)
	page.wait_for_selector(".md-preview input[data-task='2']")
	check(page.query_selector(".md-preview table td") is not None and page.query_selector(".md-preview pre") is not None, "the table and the code block")
	run.shot("preview")
	page.click(".md-preview input[data-task='2']")
	doc = "ied_shell.views.entries.get('mdpreview:' + path).instance.sync"
	page.wait_for_function("(path) => " + doc + ".pending === 0 && " + doc + ".dirty", arg=path, timeout=10000)
	page.wait_for_selector(".md-preview input[data-task='2'][checked]")
	page.evaluate("(path) => " + doc + ".save()", path)
	check("- [x] write the preview" in run.request("fs.read", {"wsId": ws, "path": path})["text"], "the box is ticked in the file")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario()
def markdown_preview_beside_is_tethered(run):
	"""Open Preview to the Side opens the preview in a group beside its file, tethered (docs/15 "Tethered tabs"): it
	scrolls with the editor, both tabs carry the link, and closing the file closes the preview."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/tether-%s.md" % run.viewport
	run.request("fs.write", {"wsId": ws, "path": path, "text": "".join("## Section %d\n\nText %d.\n\n" % (i, i) for i in range(60))})
	page.evaluate("(path) => ied_shell.commands.run('ied.editor.goTo', { path })", path)
	editor = "ied_shell.views.entries.get('doc:' + path).instance"
	page.wait_for_function("(path) => { const e = ied_shell.views.entries.get('doc:' + path); return !!(e && e.instance && e.instance.model); }", arg=path, timeout=20000)
	page.evaluate("(path) => ied_shell.commands.run('ied.markdown.previewBeside', { path })", path)
	page.wait_for_selector(".md-preview h2")
	check(len(page.query_selector_all(".dock-host:not([hidden]) .dock-group.well")) == 2, "the preview is in a group of its own")
	check(len(page.query_selector_all(".dock-host:not([hidden]) .dock-group.well .tab .tether")) == 2, "both tabs show the tether")
	page.evaluate("(path) => " + editor + ".editor.setScrollTop(600)", path)
	page.wait_for_function("document.querySelector('.md-preview').scrollTop > 100", timeout=5000)
	run.shot("beside")
	page.evaluate("(path) => ied_shell.closeTab('doc:' + path)", path)
	page.wait_for_function("!document.querySelector('.md-preview')", timeout=5000)
	check(len(page.query_selector_all(".dock-host:not([hidden]) .dock-group.well")) == 1, "its group went with it")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def markdown_image_opens_in_the_viewer(run):
	"""A workspace image in a Markdown preview (`/w/<wsId>/files/`) shows at its size when clicked (imageview.js); Escape
	ends it. The mock serves shots/screen.png (the real server any image of the workspace, a server check)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/pictures-%s.md" % run.viewport
	run.request("fs.write", {"wsId": ws, "path": path, "text": "# Pictures\n\n![the screen](../shots/screen.png)\n"})
	page.evaluate("(path) => ied_shell.commands.run('ied.markdown.preview', { path })", path)
	page.wait_for_function("(() => { const i = document.querySelector('.md-preview img[data-path]'); return i && i.complete && i.naturalWidth === 320; })()", timeout=10000)
	page.click(".md-preview img[data-path]")
	page.wait_for_selector(".image-view img")
	check(run.texts(".image-view-head .mono") == ["shots/screen.png"], "the viewer names the workspace path")
	run.shot("viewer")
	page.keyboard.press("Escape")
	page.wait_for_selector(".image-view", state="detached")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario(viewports=("desktop",))
def launched_markdown_opens_with_its_preview(run):
	"""A launch URL's #open= (what `ied open notes.md` gives, docs/10): the file opens at its place with its preview
	beside it, where a box is ticked into the file (J10)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/launched.md"
	run.request("fs.write", {"wsId": ws, "path": path, "text": "# Notes\n\n- [ ] call back\n"})
	page.goto(run.target.url + "/#open=" + ws + "/" + path + ":3")
	run.reload()
	page.wait_for_selector(".tab:has-text('Preview launched.md')")
	page.wait_for_function("(path) => { const e = ied_shell.views.entries.get('doc:' + path); return !!(e && e.instance && e.instance.model); }", arg=path, timeout=20000)
	check(page.evaluate("location.hash") == "", "the fragment is read once")
	page.click(".tab:has-text('Preview launched.md')")
	page.click(".md-preview input[data-task='2']")
	sync = "ied_shell.views.entries.get('mdpreview:' + path).instance.sync"
	page.wait_for_function("(path) => " + sync + ".pending === 0 && " + sync + ".dirty", arg=path, timeout=10000)
	page.evaluate("(path) => " + sync + ".save()", path)
	check(run.request("fs.read", {"wsId": ws, "path": path})["text"] == "# Notes\n\n- [x] call back\n", "ticked in the file")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario(viewports=("desktop",), targets=("mock",))
def build_from_the_toolbar(run):
	"""The startup project's toolbar (docs/06): its configuration dropdown shared by every window, Ctrl+Shift+B building
	with the status bar counting and the Output showing the build, and a failed build turning the status bar red - still
	red after a reload - a click on it opening the Error List."""
	page = run.open()
	page.wait_for_selector(".toolbar .tb-item select[aria-label='Configuration']")
	check(page.query_selector(".toolbar .tb-item button[aria-label='Build']") is not None, "the build button")
	page.select_option(".toolbar select[aria-label='Configuration']", "Debug")
	other = run.open(fresh=False)
	other.wait_for_function("document.querySelector(\".toolbar select[aria-label='Configuration']\") && document.querySelector(\".toolbar select[aria-label='Configuration']\").value === 'Debug'", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.click(".dock-host:not([hidden]) .dock-group")
	page.keyboard.press("Control+Shift+B")
	page.wait_for_selector(".statusbar[data-state='building']")
	run.wait_text(".statusbar", "Build demo", timeout=10000)
	run.wait_text(".output-text .line", "Build demo | Debug | x64", timeout=10000)
	check(page.is_visible(".dock-host:not([hidden]) .ah-flyout .panel-title:has-text('Output')"), "the auto-hidden Output slid out with the build")
	run.shot("building")
	page.wait_for_function("document.querySelector(\"select[aria-label='Show output from']\") && document.querySelector(\"select[aria-label='Show output from']\").value === 'build'", timeout=10000)
	run.wait_text(".statusbar", "Build succeeded", timeout=20000)
	check(page.query_selector(".statusbar[data-state='building']") is None, "the status bar is plain again")
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.wait_for_selector(".statusbar[data-state='failed']", timeout=20000)
	run.wait_text(".statusbar", "Build (with an error) failed", timeout=5000)
	run.shot("failed")
	run.reload()
	page.wait_for_selector(".statusbar[data-state='failed']", timeout=10000)
	run.wait_text(".statusbar", "Build (with an error) failed", timeout=5000)
	page.click(".statusbar [data-item='ied.tasks.status']")
	page.wait_for_selector(".tab.active:has-text('Error List'), .dock-tab.active:has-text('Error List'), [role='tab'][aria-selected='true']:has-text('Error List')", timeout=10000)


EDITOR = "ied_shell.views.entries.get('doc:src/main.c').instance.editor"


def current_line(page):
	"""The 1-based line the current-statement decoration is on, or None."""
	return page.evaluate("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d ? d.range.startLineNumber : null; })()")


@scenario(viewports=("desktop",), targets=("mock",))
def debug_a_program(run):
	"""F9 sets a breakpoint, F5 starts the startup project's configuration and stops there - the current statement shown
	in the editor, the Call Stack and Locals filled, the status bar in the debugging color - F10 steps, another window
	shows the same stop, and Shift+F5 ends the session (docs/25)."""
	page = run.open()
	open_main_c(run, page)
	page.evaluate(EDITOR + ".setPosition({ lineNumber: 6, column: 2 })")
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("F9")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-set")
	page.wait_for_selector(".toolbar button[aria-label='Start Debugging']")
	page.keyboard.press("F5")
	page.wait_for_function("(() => { const e = " + EDITOR + "; return e.getModel().getAllDecorations().some((d) => d.options.className === 'dbg-current-line'); })()", timeout=10000)
	check(current_line(page) == 6, "stopped at the breakpoint: line %s" % current_line(page))
	page.wait_for_selector(".statusbar[data-state='debugging']")
	page.wait_for_selector(".toolbar button[aria-label='Step Over (F10)']:not([disabled])")
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.callStack' })")
	run.wait_text(".cs-row", "main.c, line 6")
	check(len(page.query_selector_all("[aria-label='Call Stack'] .cs-row")) == 2, "two frames")
	run.shot("stopped")
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.locals' })")
	run.wait_text(".tree-row", "renderer")
	page.click(".tree-row:has-text('player') .tw")
	page.wait_for_function("[...document.querySelectorAll('.tree-row .lv-name')].some((e) => e.textContent === 'position')", timeout=10000)
	run.shot("locals")
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("F10")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	other = run.open(fresh=False)
	open_main_c(run, other)
	other.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("Shift+F5")
	page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
	check(current_line(page) is None, "the current statement is gone with the session")
	page.evaluate(EDITOR + ".setPosition({ lineNumber: 6, column: 2 })")
	page.keyboard.press("F9")
	page.wait_for_selector(".monaco-editor .bp-glyph", state="detached")


@scenario(viewports=("desktop",), targets=("mock",))
def debug_windows(run):
	"""Watch evaluates the workspace's expressions at each stop, a hover shows a value (DataTips), Threads lists the
	program's threads, and the Breakpoints window disables one (docs/25)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 5}]})
	run.request("watch.set", {"wsId": ws, "expressions": []})
	open_main_c(run, page)
	page.evaluate("ied_shell.commands.run('ied.debug.start')")
	page.wait_for_selector(".statusbar[data-state='debugging']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; return e.getModel().getAllDecorations().some((d) => d.options.className === 'dbg-current-line'); })()", timeout=10000)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.watch' })")
	page.fill(".watch-add", "renderer")
	page.press(".watch-add", "Enter")
	page.wait_for_function("[...document.querySelectorAll('.tree-row')].some((r) => r.textContent.includes('renderer') && r.textContent.includes('0x000001f4a2b0e000'))", timeout=10000)
	page.fill(".watch-add", "nonsense")
	page.press(".watch-add", "Enter")
	page.wait_for_selector(".tree-row .lv-value.c-error", timeout=10000)
	run.shot("watch")
	box = page.evaluate("(() => { const e = " + EDITOR + "; const p = e.getScrolledVisiblePosition({ lineNumber: 6, column: 11 }); const r = e.getDomNode().getBoundingClientRect(); return { x: r.left + p.left + 4, y: r.top + p.top + p.height / 2 }; })()")
	page.mouse.move(box["x"], box["y"])
	page.wait_for_selector(".monaco-hover:has-text('player = ')", timeout=10000)
	run.shot("hover")
	page.mouse.move(5, 5)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.threads' })")
	run.wait_text(".cs-row", "main")
	page.keyboard.press("Control+Alt+B")
	page.wait_for_selector(".bp-grid.cs-row:has-text('main.c, line 6')")
	page.click(".bp-grid.cs-row input[type='checkbox']")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-disabled", timeout=10000)
	run.shot("breakpoints")
	page.evaluate("ied_shell.commands.run('ied.debug.stop')")
	page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})
	run.request("watch.set", {"wsId": ws, "expressions": []})


@scenario(targets=("mock",))
def diagnostic_tools_while_debugging(run):
	"""Diagnostic Tools (F-DBG-52, docs/25): the debugging layout shows it on the right; it names the program's process
	and graphs its memory and CPU as the server samples them, busy while it runs and idle at a breakpoint; the samples
	stay to look at after the session ends."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 9}]})
	try:
		open_main_c(run, page)
		page.evaluate("ied_shell.commands.run('ied.debug.start')")
		page.wait_for_selector(".statusbar[data-state='debugging']")
		page.wait_for_selector(".dock-host:not([hidden]) .dock .panel-title:has-text('Diagnostic Tools')")
		run.wait_text(".dt-head", "demo.exe (4242)", timeout=10000)
		page.wait_for_function("ied_shell.views.entries.get('ied.diagnosticTools').instance.usage.samples.length >= 4", timeout=10000)
		check("MB private" in run.texts(".dt-value")[0], "the memory now: %s" % run.texts(".dt-value"))
		box = page.locator(".dock-host:not([hidden]) .dock-group.tool:has(.panel-title:has-text('Diagnostic Tools'))").bounding_box()
		well = page.locator(".dock-host:not([hidden]) .dock-group.well").first.bounding_box()
		check(box["x"] > well["x"] + well["width"] - 2, "on the right of the documents: %s" % box)
		drawn = page.evaluate("[...document.querySelectorAll('.dt-graph')].map((c) => c.getContext('2d').getImageData(0, 0, c.width, c.height).data.some((v, i) => i % 4 === 3 && v > 0))")
		check(drawn == [True, True], "both graphs drawn: %s" % drawn)
		run.shot("graphs")
		page.click(".dock-host:not([hidden]) .dock-group.well .monaco-editor .view-lines")
		page.keyboard.press("Control+Alt+F2")
		page.wait_for_function("document.activeElement.closest('.view') && document.activeElement.closest('.view').dataset.ref === 'ied.diagnosticTools'", timeout=5000)
		page.evaluate("ied_shell.commands.run('ied.debug.stop')")
		page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
		page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.diagnosticTools' })")
		page.wait_for_selector(".dock-host:not([hidden]) .ah-flyout .dt-graphs:not([hidden])")
		check(run.texts(".dt-head")[0].startswith("Diagnostics session"), "the ended session's samples are still there")
		time.sleep(0.3)
		run.shot("after")
	finally:
		run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})


@scenario(viewports=("phone",), targets=("mock",))
def debug_on_a_phone(run):
	"""On a phone the run controls are in the toolbar strip: Start Debugging stops at the breakpoint with the statement
	shown in the editor, Step Over moves it, the Call Stack is a pane, and Stop ends the session."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 5}]})
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'src/main.c', line: 5 })")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-set", timeout=20000)
	page.tap(".phone-toolbar button[aria-label='Start Debugging']")
	page.wait_for_selector(".phone-toolbar[data-state='debugging']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 6; })()", timeout=10000)
	page.wait_for_selector(".phone-toolbar button[aria-label='Step Over (F10)']:not([disabled])")
	run.shot("stopped")
	page.tap(".phone-toolbar button[aria-label='Step Over (F10)']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.callStack' })")
	run.wait_text(".cs-row", "main.c, line 7")
	run.shot("call-stack")
	page.tap(".phone-toolbar button[aria-label='Stop Debugging (Shift+F5)']")
	page.wait_for_selector(".phone-toolbar[data-state='debugging']", state="detached", timeout=10000)
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})


def terminal_text(page):
	"""What the shown terminal's screen says (xterm's rows)."""
	return page.evaluate("[...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].map((r) => r.textContent).join('\n')")


def type_in_terminal(page, text):
	page.click(".term-view:not([hidden]) .xterm-screen")
	page.keyboard.type(text)
	page.keyboard.press("Enter")


@scenario(viewports=("desktop",))
def terminal_dock(run):
	"""The terminal dock under every activity (docs/15): New Terminal opens it with a shell on the server, typed commands
	run, another window shows the same screen and takes over typing, Ctrl+` collapses it to its header, Kill ends it."""
	page = run.open()
	page.wait_for_selector(".panel-dock .term-head")
	check(page.query_selector(".panel-dock.collapsed") is not None, "collapsed to its header at first")
	page.click(".term-head button[aria-label='New Terminal']")
	page.wait_for_selector(".panel-dock:not(.collapsed) .term-tab.on")
	page.wait_for_selector(".term-view:not([hidden]) .xterm-screen")
	marker = "ied-e2e-" + run.viewport
	type_in_terminal(page, "echo " + marker)
	page.wait_for_function("(m) => [...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].filter((r) => r.textContent.includes(m)).length >= 2", arg=marker, timeout=30000)
	run.shot("typed")
	other = run.open(fresh=False)
	other.wait_for_selector(".term-tab")
	other.click(".term-tab")
	other.wait_for_function("(m) => [...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].some((r) => r.textContent.includes(m))", arg=marker, timeout=20000)
	other.wait_for_selector(".term-view:not([hidden]) .term-bar:not([hidden])")
	other.click(".term-view:not([hidden]) .term-bar button")
	other.wait_for_selector(".term-view:not([hidden]) .term-bar", state="hidden")
	page.wait_for_selector(".term-view:not([hidden]) .term-bar:not([hidden])", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.click(".term-head")
	page.keyboard.press("Control+`")
	page.wait_for_selector(".panel-dock.collapsed")
	page.keyboard.press("Control+`")
	page.wait_for_selector(".panel-dock:not(.collapsed)")
	page.click(".term-head button[aria-label='Kill Terminal']")
	page.wait_for_selector(".term-tab", state="detached", timeout=10000)
	check(page.is_visible(".term-empty"), "says how to open one")


@scenario(viewports=("phone",), targets=("mock",))
def terminal_on_a_phone(run):
	"""On a phone the terminal is a slide-up drawer from More, with a row of the keys a phone keyboard lacks."""
	page = run.open()
	page.tap(".phone-nav button[aria-label='More']")
	page.tap(".menu [role='menuitem']:has-text('Terminal')")
	page.wait_for_selector(".panel-dock.drawer.shown")
	page.tap(".term-head button[aria-label='New Terminal']")
	page.wait_for_selector(".term-view:not([hidden]) .xterm-screen")
	page.wait_for_selector(".term-keys button:has-text('Tab')")
	type_in_terminal(page, "echo from-the-phone")
	page.wait_for_function("[...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].filter((r) => r.textContent.includes('from-the-phone')).length >= 2", timeout=20000)
	run.shot("drawer")
	page.tap(".term-head button[aria-label='Kill Terminal']")
	page.wait_for_selector(".term-tab", state="detached", timeout=10000)
	page.tap(".term-head button[aria-label='Hide the panel']")
	page.wait_for_selector(".panel-dock.drawer.shown", state="detached")


@scenario(viewports=("desktop",), targets=("mock",))
def output_links_open_places(run):
	"""A place an Output line names is underlined and a click opens it (VS): a compiler's error opens its file at the line."""
	page = run.open()
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.evaluate("ied_shell.commands.run('ied.output.show', { sourceId: 'build' })")
	page.wait_for_selector(".output-text .out-loc:has-text('player.c(11,61)')", timeout=20000)
	run.shot("links")
	page.click(".output-text .out-loc:has-text('player.c(11,61)')")
	page.wait_for_function("(() => { const e = ied_shell.views.entries.get('doc:src/game/player.c'); return !!(e && e.instance && e.instance.editor && e.instance.editor.getPosition() && e.instance.editor.getPosition().lineNumber === 11); })()", timeout=20000)


@scenario(viewports=("phone",), targets=("mock",))
def build_from_the_phone_strip(run):
	"""On a phone the toolbar views sit in a strip above the bottom nav (docs/15 "Phone"), colored like the status bar:
	a tap on Build shows the build's output, the strip is blue while it runs and red after a failed build; More offers
	the Command Palette for everything else."""
	page = run.open()
	page.wait_for_selector(".phone-toolbar select[aria-label='Configuration']")
	config = page.eval_on_selector(".phone-toolbar select[aria-label='Configuration']", "el => el.value")
	page.tap(".phone-toolbar button[aria-label='Build']")
	page.wait_for_selector(".phone-toolbar[data-state='building']")
	run.wait_text(".output-text .line", "Build demo | %s | x64" % config, timeout=10000)
	run.shot("building")
	page.wait_for_selector(".phone-toolbar[data-state='idle']", timeout=20000)
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.wait_for_selector(".phone-toolbar[data-state='failed']", timeout=20000)
	run.shot("failed")
	page.tap(".phone-nav button[aria-label='More']")
	page.wait_for_selector(".menu [role='menuitem']:has-text('Command Palette'), .sheet :text('Command Palette')", timeout=5000)
	run.shot("more")


def restore(run, page, original):
	"""Puts main.c back as it was (through the server, so every window follows) and saves it."""
	sync = "ied_shell.views.entries.get('doc:src/main.c').instance.sync"
	settled(page)
	current = editor_text(page)
	lines = current.split("\n")
	whole = {"range": {"start": {"line": 0, "col": 0}, "end": {"line": len(lines) - 1, "col": len(lines[-1])}}, "text": original}
	run.request("testhooks.serverEdit", {"docId": page.evaluate(sync + ".docId"), "changes": [whole]})
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue() === " + json.dumps(original), timeout=10000)
	page.evaluate(sync + ".save()")
	check(server_text(run) == original, "main.c is back as it was")


@scenario(viewports=("desktop",))
def two_windows_share_a_document(run):
	first = run.open()
	open_main_c(run, first)
	take_over(first)
	original = editor_text(first)
	first.click(".monaco-editor .view-lines")
	first.keyboard.press("Control+Home")
	first.keyboard.type("A")
	second = run.open(fresh=False)
	open_main_c(run, second)
	second.wait_for_selector(".ed-bar:has-text('another window is editing')")
	run.shot("read-only")
	first.keyboard.type("B")
	second.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue().startsWith('AB')", timeout=10000)
	second.click(".ed-bar button:has-text('Edit Here')")
	second.wait_for_selector(".ed-bar", state="hidden")
	first.wait_for_selector(".ed-bar:has-text('another window is editing')")
	second.click(".monaco-editor .view-lines")
	second.keyboard.press("Control+Home")
	second.keyboard.press("End")
	second.keyboard.type("C")
	first.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue().split('\\n')[0].endsWith('C')", timeout=10000)
	run.page = first
	run.shot("mirrored")
	settled(second)
	check(editor_text(first) == editor_text(second), "both windows show the same text")
	restore(run, second, original)


@scenario(viewports=("desktop",))
def server_edits_while_typing(run):
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	original = editor_text(page)
	doc_id = page.evaluate("ied_shell.views.entries.get('doc:src/main.c').instance.sync.docId")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+End")
	for word in ("one ", "two ", "three "):
		page.keyboard.type(word)
		run.request("testhooks.serverEdit", {"docId": doc_id, "changes": [{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "// ai\n"}]})
	settled(page)
	time.sleep(0.5)
	text = editor_text(page)
	check(text.count("// ai\n") == 3 and text.rstrip().endswith("one two three"), "both writers' text: %r" % text[-80:])
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(server_text(run) == editor_text(page), "the editor and the server agree")
	run.shot("merged")
	restore(run, page, original)


# ---------------------------------------------------------------------------------------------------------------
# Phone

@scenario(viewports=("phone",))
def phone_panes(run):
	page = run.open()
	slots = run.texts(".phone-nav .pn > span:not(.ci)")
	check(slots[:4] == ["Chat", "Editor", "Git", "Solution Explorer"] and slots[-1] == "More", "the slots: %s" % slots)
	page.click(".phone-nav .pn[data-pane='ied.explorer']")
	page.wait_for_selector(".phone-pane .tree-row:has-text('README.md')")
	run.shot("explorer")
	page.click(".phone-nav .pn[aria-label='More']")
	page.click(".menu.sheet .menu-item:has-text('Output')")
	page.wait_for_selector(".phone-pane .output-text")
	run.shot("output")
	page.click(".phone-nav .pn[data-pane='ied.explorer']")
	page.click(".phone-pane .tree-row:has-text('README.md')")
	page.wait_for_selector(".phone-pane .monaco-editor .view-lines", timeout=30000)
	check(page.is_visible(".phone-head button[aria-label='Back']"), "a back button")
	check("README.md" in run.texts(".phone-head .t b"), "the pane title")
	run.shot("document")
	page.click(".phone-head button[aria-label='Back']")
	page.wait_for_selector(".phone-pane .tree-row")
	page.click(".phone-nav .pn[aria-label='More']")
	page.wait_for_selector(".menu.sheet")
	more = run.texts(".menu.sheet .menu-item .label")
	check("Keyboard Shortcuts" in more and "Settings" in more, "More lists the rest, Settings among it (no gear on a phone)")
	run.shot("more")


# ---------------------------------------------------------------------------------------------------------------
# Chat (docs/10 "Chats"): the mock plays its scripted turns; the real server talks to tools/fake-model.

def model_script(*steps):
	return {"description": "e2e", "stopClass": "none", "harness": "-", "responses": [{"chunkDelayMs": 20, "steps": [{"role": "assistant"}] + list(steps) + [{"usage": True}, {"done": True}]}]}


def open_chat(run):
	"""The Chat activity on a new Ask chat (`run.chat_id`), opened from the Chats list; on the real server with the fake
	model."""
	page = run.open()
	if run.target.kind == "real":
		run.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": run.target.model_url + "/v1", "watchdog": {"prefillRate": 1000000}}}})
	ws_id = page.evaluate("ied_shell.wsId")
	run.chat_id = run.request("chat.create", {"wsId": ws_id, "mode": "ask"})["chatId"]
	show_chat(run)
	return page


def show_chat(run):
	"""`run.chat_id` shown from the Chats list (the Chat activity's tool window; on a phone its pane)."""
	run.show_activity("ied.chat")
	run.page.click(".chats-row[data-chat='%s']" % run.chat_id)
	run.page.wait_for_selector(".view[data-ref='chat:%s'] .chat-input" % run.chat_id)
	run.page.wait_for_function("!ied_shell.layout.dirty && !ied_shell.layout.saving", timeout=10000)


def streaming(page):
	page.wait_for_function("(document.querySelector('.chat-turn:last-child .chat-text') || {textContent: ''}).textContent.length > 40", timeout=20000)


def send(page, text):
	page.fill(".chat-input", text)
	page.press(".chat-input", "Enter")


@scenario(viewports=("desktop", "phone"))
def chat_turn_with_approval(run):
	page = open_chat(run)
	if run.target.kind == "real":
		edit = {"path": "src/main.c", "edits": [{"old": "int main(void)", "new": "/* the entry point */\nint main(void)"}]}
		run.target.queue_model(
			model_script({"text": "I'll document the entry point:", "chunk": 6}, {"toolCall": {"name": "fs_edit", "arguments": edit}}, {"finish": "tool_calls"}),
			model_script({"text": "Done: `main` is documented.", "chunk": 6}, {"finish": "stop"}))
	send(page, "clamp the velocity")
	page.wait_for_selector(".chat-approval .chat-diff .dl.add", timeout=20000)
	if not run.phone:
		page.wait_for_selector(".tab[data-ref='chat:%s'][data-icon*='shield']" % run.chat_id)
		check(page.is_visible(".chats-row.attn[data-chat='%s']" % run.chat_id), "the tab and the Chats list say the chat waits on its owner")
	run.shot("approval")
	page.click(".chat-approval .btn.primary")
	page.wait_for_selector(".chat-foot:has-text('Finished')", timeout=30000)
	states = page.eval_on_selector_all(".chat-tool, .chat-finish", "els => els.map(e => e.dataset.state || 'finish')")
	check("ok" in states and "awaiting_approval" not in states, "the edit ran: %s" % states)
	if not run.phone:
		check(page.is_visible(".chat-panel .chat-sec-row.file"), "the changed file is listed in the panel")
	run.shot("finished")


@scenario(viewports=("desktop", "phone"), targets=("real",))
def chat_clipboard(run):
	"""The AI copies lines of a file and pastes them into a later call (docs/13 `clipboard`): the cards say what was
	copied and where it was pasted, and the details panel lists the clip, which unfolds to its text. The file tools'
	side (fs_edit, fs_write, an unknown clip, a restart) is a server check (test_chat)."""
	page = open_chat(run)
	entry = {"name": "entry", "path": "src/main.c", "from": "int main", "to": "}"}
	call = {"name": "call", "path": "src/main.c", "from": "core_run", "to": "("}
	run.target.queue_model(
		model_script({"text": "Copying the entry point:", "chunk": 6}, {"toolCall": {"name": "clipboard_copy", "arguments": entry}}, {"finish": "tool_calls"}),
		model_script({"toolCall": {"name": "clipboard_copy", "arguments": call}}, {"finish": "tool_calls"}),
		model_script({"toolCall": {"name": "search_text", "arguments": {"pattern": "{{clip:call}}"}}}, {"finish": "tool_calls"}),
		model_script({"text": "`core_run` is called from `main`.", "chunk": 6}, {"finish": "stop"}))
	send(page, "who calls core_run?")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
	states = page.eval_on_selector_all(".chat-turn:last-child .chat-tool", "els => els.map(e => e.dataset.state)")
	check(states == ["ok", "ok", "ok"], "the copies and the search ran: %s" % states)
	check("entry: src/main.c" in run.texts(".chat-turn:last-child .chat-tool")[0], "the copy's card names the clip and the file")
	page.click(".chat-turn:last-child .chat-tool >> nth=2 >> .chat-tool-head")
	run.wait_text(".chat-turn:last-child .chat-tool-out", "matches")
	found = run.texts(".chat-turn:last-child .chat-tool-out")[0]
	matches = found[found.rindex("}") + 1:].split(" ")[0]
	check("{{clip:call}}" in found and int(matches) > 0, "the card keeps the token; the search found the pasted text: %s" % found)
	run.shot("pasted")
	if run.phone:
		page.click("button[aria-label='Chat details']")
	page.wait_for_selector(".chat-panel .chat-sec[data-section='clips'] .chat-sec-row.clip[data-clip='entry']")
	page.click(".chat-sec-row.clip[data-clip='entry']")
	page.wait_for_selector(".chat-panel .chat-clip")
	check(run.texts(".chat-panel .chat-clip")[0].startswith("int main(void)\n{"), "the clip unfolds to the text it holds")
	run.shot("clipboard")


@scenario(viewports=("desktop", "phone"), targets=("real",))
def chat_effort(run):
	"""A chat's effort from the composer (docs/04 "Effort"): its group's is named until the chat picks one, and the pick
	goes with the next request (the fake model is a llama.cpp server, so as its chat template reads it). At phone width
	the selectors scroll beside the box and the menu is a sheet."""
	page = open_chat(run)
	run.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {"local": {"primary": {"model": "fake", "effort": "medium"}}}})
	run.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": "local"})
	try:
		run.reload()
		show_chat(run)
		run.wait_text(".chat-selectors", "Medium (group)")
		page.click(".chat-selectors .chat-dd[title='Effort']")
		page.wait_for_selector(".popup.menu" + (".sheet" if run.phone else ""))
		check("As the group says (Medium)" in run.texts(".popup.menu")[0], "the group's row names its effort")
		run.shot("menu")
		page.click(".popup.menu .menu-item:has-text('Low')")
		run.wait_text(".chat-selectors .chat-dd[title='Effort']", "Low")
		run.target.queue_model(model_script({"text": "Quick.", "chunk": 6}, {"finish": "stop"}))
		send(page, "hi")
		page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
		sent = run.target.model_requests()[-1]["body"].get("chat_template_kwargs")
		check(sent == {"reasoning_effort": "low"}, "the chat's pick was sent: %s" % sent)
		run.shot("sent")
	finally:
		run.request("settings.set", {"tier": "app", "key": "ai.groups", "value": {}})
		run.request("settings.set", {"tier": "app", "key": "ai.defaultGroup", "value": ""})


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def chat_shows_the_prompt_read(run):
	"""While the model's server reads a long prompt the turn shows how far (llama.cpp's progress, turn.progress), and
	the bar goes once the reply streams. The real server's side is a server check (test_chat)."""
	page = open_chat(run)
	send(page, "read this big prompt")
	page.wait_for_selector(".chat-turn:last-child .chat-prefill:not([hidden]) .chat-bar", timeout=20000)
	check("Reading the prompt" in run.texts(".chat-turn:last-child .chat-prefill")[0], "it says what is happening")
	run.shot("prefill")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
	check(page.is_hidden(".chat-turn:last-child .chat-prefill"), "gone once the reply came")


@scenario(viewports=("desktop",), targets=("mock",))
def chat_shows_an_estimated_read(run):
	"""A server that does not report its reading (not llama.cpp): the turn shows the estimate, its bar moving with the
	clock."""
	page = open_chat(run)
	send(page, "ask the slow server")
	page.wait_for_selector(".chat-turn:last-child .chat-prefill:not([hidden]) .chat-bar", timeout=20000)
	check("(estimated)" in run.texts(".chat-turn:last-child .chat-prefill")[0], "it says it is an estimate")
	bar = "document.querySelector('.chat-turn:last-child .chat-prefill .chat-bar i').style.width"
	first = page.evaluate(bar)
	page.wait_for_function("(w) => " + bar + " !== w", arg=first, timeout=5000)
	run.shot("estimate")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def chat_tool_image(run):
	"""A tool's result shows an image (docs/13 "Images in tool results"): the card shows it, open or closed. The real
	server's side is a server check (test_chat), which needs a plugin with code."""
	page = open_chat(run)
	send(page, "what is on the screen?")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=20000)
	page.wait_for_function("(() => { const i = document.querySelector('.chat-tool-images img'); return i && i.complete && i.naturalWidth === 320; })()", timeout=10000)
	check(page.get_attribute(".chat-tool-images a", "title") == "shots/screen.png", "the image's path on hover")
	run.shot("image")
	page.click(".chat-tool-images a")
	page.wait_for_function("(() => { const i = document.querySelector('.image-view img'); return i && i.complete && i.naturalWidth === 320; })()", timeout=10000)
	check(run.texts(".image-view-head .mono") == ["shots/screen.png"], "the viewer names it")
	run.shot("viewer")
	page.keyboard.press("Escape")
	page.wait_for_selector(".image-view", state="detached")


@scenario(viewports=("desktop",))
def chat_tabs_dock(run):
	"""Chats are dockable tabs (docs/15 "Chat activity"): a second chat splits beside the first, and a chat moves to the
	Editor's well from its tab's menu, titled by the chat, where a turn runs from its own composer; the Chats list
	still counts it open."""
	page = open_chat(run)
	chat_id = run.chat_id
	other = run.request("chat.create", {"wsId": page.evaluate("ied_shell.wsId"), "mode": "ask", "title": "Second"})["chatId"]
	page.click(".chats-row[data-chat='%s']" % other)
	page.click(".tab[data-ref='chat:%s']" % other, button="right")
	page.click(".menu .menu-item:has-text('Move to New Group Right')")
	page.wait_for_function("document.querySelectorAll(\".dock-host[data-activity='ied.chat'] .dock-group.well\").length === 2")
	check(len(page.query_selector_all(".chats-sec[data-section='open'] .chats-row.shown")) == 2, "both chats show, each in its group")
	run.shot("split")
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": "Answered in the tab.", "chunk": 6}, {"finish": "stop"}))
	page.click(".tab[data-ref='chat:%s']" % chat_id, button="right")
	page.click(".menu .menu-item:has-text('Move to Editor')")
	tab = ".dock-host[data-activity='ied.editor'] .dock-group.well .tab[data-ref='chat:%s']" % chat_id
	page.wait_for_selector(tab)
	view = ".view[data-ref='chat:%s']" % chat_id
	page.fill(view + " .chat-input", "answer here")
	page.press(view + " .chat-input", "Enter")
	page.wait_for_selector(view + " .chat-foot:has-text('Finished')", timeout=30000)
	check(page.inner_text(tab + " .name") not in ("", "Chat"), "the tab is titled by the chat: %s" % page.inner_text(tab + " .name"))
	run.shot("tab")
	run.show_activity("ied.chat")
	check(page.is_visible(".chats-sec[data-section='open'] .chats-row[data-chat='%s']" % chat_id), "the Chats list counts a chat open in another activity")


@scenario(viewports=("desktop", "phone"))
def chat_delete_and_restore(run):
	"""Deleting a chat keeps it (docs/04 "Deleting a chat"): it moves under Deleted in the Chats list with how long it is
	kept, its tab closes and the toast offers Undo; Restore brings it back with its turns and opens it."""
	page = open_chat(run)
	chat_id = run.chat_id
	run.request("chat.set", {"chatId": chat_id, "title": "Doomed"})
	run.show_activity("ied.chat")
	row = ".chats-row[data-chat='%s']" % chat_id
	page.click(row + ":not(.deleted)", button="right")
	page.click(".menu .menu-item:has-text('Delete')")
	page.wait_for_selector(".toast:has-text('Doomed') .toast-action:has-text('Undo')")
	page.wait_for_selector(".tab[data-ref='chat:%s']" % chat_id, state="detached")
	if not page.is_visible(row + ".deleted"):
		page.click(".chats-sec-head:has-text('Deleted')")
	page.wait_for_selector(row + ".deleted:has-text('30 days left')")
	run.shot("deleted")
	page.hover(row + ".deleted")
	page.click(row + ".deleted .chats-restore")
	page.wait_for_selector(".view[data-ref='chat:%s'] .chat-input" % chat_id)
	run.show_activity("ied.chat")
	check(not page.is_visible(row + ".deleted"), "restored: no longer under Deleted")
	run.shot("restored")


@scenario(viewports=("desktop",))
def chat_reload_mid_turn(run):
	page = open_chat(run)
	text = " ".join("Sentence %d of a long answer." % i for i in range(1, 301))
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": text, "chunk": 8, "delayMs": 15}, {"finish": "stop"}))
	send(page, "write a long essay")
	streaming(page)
	page.reload()
	run.wait_ready()
	page.wait_for_selector(".chat-act", timeout=20000)
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=60000)
	if run.target.kind == "real":
		run.wait_text(".chat-turn:last-child .chat-text", "Sentence 300 of a long answer.")
	else:
		page.wait_for_function("document.querySelectorAll('.chat-turn:last-child .chat-text li').length === 40", timeout=10000)
	run.shot("finished")


@scenario(viewports=("desktop",))
def chat_stop_and_queue(run):
	page = open_chat(run)
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": "word " * 2000, "chunk": 5, "delayMs": 20}, {"finish": "stop"}), model_script({"text": "The queued answer.", "chunk": 6}, {"finish": "stop"}))
	send(page, "write a long essay")
	streaming(page)
	send(page, "and then summarize it")
	page.wait_for_selector(".chat-queued:has-text('and then summarize it')")
	run.shot("queued")
	page.click(".chat-row2 .btn:has-text('Stop')")
	page.wait_for_selector(".chat-foot:has-text('Stopped')", timeout=20000)
	check(page.is_visible(".chat-queued"), "a stopped turn leaves the queue for the owner")
	run.shot("stopped")
	page.click(".chat-queued button[aria-label='Send the queued message now']")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
	check(not page.is_visible(".chat-queued") and "and then summarize it" in run.texts(".chat-turn:last-child .chat-user"), "the queued message was sent")


PIXEL_PNG = bytes.fromhex(
	"89504e470d0a1a0a0000000d494844520000001000000010080200000090916836000001964944415478da15d151154421084551231881"
	"084630021188608413c1084620021188400422cc1bbfd9accb758cc11cc8600df640073638030677f0063e88410e6ad083312673229335"
	"d9139dd8e44c98dcc99bf8242639a949cf0f08531061095b50c18423205ce1092e849042092d1f58cc852cd6622f74618bb36071176fe1"
	"8b58e4a216bd3eb0991bd9accddee8c63667c3e66edec637b1c94d6d7a7f40998a284bd98a2aa61c05e52a4f712594544a69fd80310d31"
	"96b10d35cc3806c6359ee146186994d1f681c33cc8611df6410f7638070ef7f00e7e88431eead0e703ff02bf4abe23bfd85f906ff537fc"
	"7f171e38042414f4f73de3322f7259977dd18b5dcefd8fdfcbbbf8252e79a94bdf0f3ce6431eebb11ffab0c779ffe5f7f11efe88473eea"
	"d1ef03ce74c459ce76d431e7f83fca759ee34e38e994d3fe81600612ac60071a5870e21ffc062ff020820c2ae8f84032134956b2134d2c"
	"39f93ff3262ff124924c2ae9fc40310b2956b10b2dac38f52fe516aff0228a2caae8fa40331b6956b31b6dac39fdaff036aff1269a6caa"
	"e9e607880270100b735fc00000000049454e44ae426082")


@scenario(viewports=("desktop", "phone"))
def chat_mentions_and_attachments(run):
	"""@ in the composer lists the workspace's files and more (docs/04 "Attachments and @-mentions"): Enter writes the
	mention; a picture attached shows as a chip; the sent message shows both under its text, and the model was given
	the file's text and the picture."""
	page = open_chat(run)
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": "I read src/main.c and looked at the picture.", "chunk": 6}, {"finish": "stop"}))
	page.click(".chat-input")
	page.keyboard.type("Look at @src/mai")
	page.wait_for_selector(".chat-mention-list:not([hidden]) .chat-mention-row.on:has-text('@src/main.c')", timeout=10000)
	run.shot("mention-list")
	page.keyboard.press("Enter")
	check(page.input_value(".chat-input") == "Look at @src/main.c ", "the mention is written: %r" % page.input_value(".chat-input"))
	check(page.is_hidden(".chat-mention-list"), "the list closes")
	page.set_input_files(".chat-composer input[type=file]", files=[{"name": "shot.png", "mimeType": "image/png", "buffer": PIXEL_PNG}])
	page.wait_for_selector(".chat-attached .chat-attach:not(.uploading) img", timeout=10000)
	page.keyboard.type("and this picture")
	run.shot("composer")
	page.keyboard.press("Enter")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
	chips = run.texts(".chat-turn:last-child .chat-chips .chat-chip")
	check(len(chips) == 2 and "@src/main.c" in chips[0] and "shot.png" in chips[1], "the message shows what it named and carried: %s" % chips)
	page.wait_for_function("(() => { const i = document.querySelector('.chat-turn:last-child .chat-chip.image img'); return i && i.complete && i.naturalWidth === 16; })()", timeout=10000)
	check(page.is_hidden(".chat-attached"), "the composer's chips are gone once sent")
	if run.target.kind == "real":
		user = [m for m in run.target.model_requests()[-1]["body"]["messages"] if m["role"] == "user"][0]
		parts = user["content"] if isinstance(user["content"], list) else [{"type": "text", "text": user["content"]}]
		check('<mention label="@src/main.c" kind="file">' in parts[0]["text"] and "int main" in parts[0]["text"], "the model got the file's text")
	run.shot("sent")


# ---------------------------------------------------------------------------------------------------------------
# Settings (docs/07 "The settings window as built")

def open_settings(run, page):
	if run.phone:
		page.evaluate("ied_shell.commands.run('shell.settings')")
	else:
		page.click(".sidebar button[aria-label='Manage']")
		page.click(".menu .menu-item:has-text('Settings')")
	page.wait_for_selector(".settings-window .set-node")


@scenario(viewports=("desktop", "phone"))
def settings_window(run):
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='page:AI']")
	field = ".set-row[data-key='ai.maxSteps'] input"
	page.fill(field, "120")
	page.press(field, "Enter")
	page.wait_for_selector(".set-row[data-key='ai.maxSteps'] .set-source.set:has-text('User')")
	page.fill(field, "-1")
	page.press(field, "Enter")
	run.wait_text(".set-row[data-key='ai.maxSteps'] .set-message", "less than minimum")
	run.shot("refused")
	page.click(".set-row[data-key='ai.maxSteps'] button:has-text('Reset')")
	page.wait_for_selector(".set-row[data-key='ai.maxSteps'] .set-source:not(.set):has-text('Default')")
	page.fill(".set-search", "clangd")
	page.wait_for_selector(".set-row[data-key='cpp.clangd.path']")
	check(not page.is_visible(".set-row[data-key='ai.maxSteps']"), "search keeps only what matches")
	run.shot("search")


@scenario(viewports=("desktop", "phone"), targets=("real",))
def developer_settings_page(run):
	"""Environment > Developer (docs/28): the setting drawn as every row, the checkout the server is built from, and
	Rebuild; a browser is offered no relaunch. Nothing is built: Rebuild would install this checkout."""
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='custom:ied.developer']")
	page.wait_for_selector(".set-custom[data-page='custom:ied.developer'] .set-row[data-key='developer.enabled']")
	run.wait_text(".dev-rows", "Source checkout")
	run.wait_text(".dev-rows", "HEAD ")
	check(page.is_visible(".dev-actions button:has-text('Rebuild')"), "a checkout can be rebuilt")
	check(not page.is_visible(".dev-actions button:has-text('Rebuild and relaunch')"), "a browser cannot relaunch")
	run.shot("developer")


@scenario(viewports=("desktop", "phone"), targets=("real",))
def web_search_settings_page(run):
	"""Web > Search (docs/29): SearXNG's and Tor's state from their topics, the engines with SearXNG's categories, Test
	engines, and the window's own rows for the other keys; Web > Tor's generic rows. Nothing is searched: that needs the
	network and Tor."""
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='custom:ied.websearch.search']")
	pane = ".set-custom[data-page='custom:ied.websearch.search']"
	run.wait_text(pane + " .ws-state", "next search starts it")
	check(page.locator(pane + " .ws-engine").count() == 7, "the default engines")
	if not run.phone:
		page.wait_for_selector(pane + " .ws-engine[data-engine='wikipedia'] .ws-engine-cats:has-text('general')")
	check(page.is_visible(pane + " button:has-text('Test engines')"), "engines can be tested")
	page.wait_for_selector(pane + " .set-row[data-key='websearch.useTor'] input[type='checkbox']")
	check(not page.is_visible(pane + " .set-row[data-key='websearch.engines']"), "the engines are drawn by the page, not as a row")
	run.shot("search")
	page.click(".set-node[data-page='page:Web/Tor']")
	page.wait_for_selector(".set-row[data-key='tor.exitCountries']")
	run.shot("tor")


@scenario(viewports=("desktop",))
def model_settings(run):
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='custom:ied.chat.models']")
	page.click(".set-bar button:has-text('Add model')")
	endpoint = run.target.model_url + "/v1" if run.target.kind == "real" else "http://127.0.0.1:8080/v1"
	page.fill(".set-model.editing input[aria-label='Name']", "local-test")
	page.fill(".set-model.editing input[aria-label='Endpoint']", endpoint)
	page.fill(".set-model.editing input[aria-label='Model id']", "fake-model")
	page.click(".set-model.editing button:has-text('Test connection')")
	page.wait_for_selector(".set-model.editing .set-probe.ok", timeout=15000)
	run.shot("tested")
	page.click(".set-model.editing button:has-text('Save')")
	page.wait_for_selector(".set-model[data-model='local-test']:not(.editing)")
	page.click(".set-node[data-page='custom:ied.chat.groups']")
	page.click(".set-bar button:has-text('New')")
	page.wait_for_selector(".set-role[data-role='primary'][data-state='on']")
	check(page.is_visible(".set-role[data-role='planning'][data-state='off']"), "a new group turns on primary alone")
	page.click(".set-role[data-role='planning'] .set-role-toggle")
	page.wait_for_selector(".set-role[data-role='planning'][data-state='on']")
	primary = page.eval_on_selector(".set-role[data-role='primary'] .set-role-model", "e => e.value")
	check(primary and page.eval_on_selector(".set-role[data-role='planning'] .set-role-model", "e => e.value") == primary, "a role turned on gets the primary's model")
	page.click(".set-bar button:has-text('Make default')")
	page.wait_for_selector(".set-group.on .ci")
	run.shot("groups")
	groups = run.request("settings.get", {"wsId": page.evaluate("ied_shell.wsId")})["values"]
	check(groups["ai.defaultGroup"]["value"] == "group" and groups["ai.groups"]["value"]["group"]["planning"]["model"] == primary, "written to the User tier: %s" % groups["ai.groups"])


# ---------------------------------------------------------------------------------------------------------------
# Git (docs/10 "Git", mockup 05): the mock's canned repository; the real server's is the fixture made a repository
# (targets.make_repository).

def open_git(run):
	page = run.open()
	if run.target.kind == "real":
		run.request("ws.setTrust", {"wsId": page.evaluate("ied_shell.wsId"), "trusted": True})
	run.show_activity("ied.git")
	if run.phone:
		page.click(".phone-nav .pn[data-pane='activity:ied.git']")
	page.wait_for_selector(".git-row:not(.local)", timeout=20000)
	return page


@scenario(viewports=("desktop",))
def git_stage_commit_branch_search(run):
	page = open_git(run)
	real = run.target.kind == "real"
	path = "src/e2e-notes.txt" if real else "src/game/player.c"
	if real:
		with open(os.path.join(run.target.workspace, "src", "e2e-notes.txt"), "w", encoding="utf-8") as f:
			f.write("from the e2e run" + chr(10))
	page.wait_for_selector(".git-row.local")
	check("main" in run.texts(".git-row .ref.head") if real else "port/logging" in run.texts(".git-row .ref.head"), "HEAD's branch labels its commit")
	run.shot("graph")
	page.click(".git-row.local")
	row = ".git-changes .git-file[data-path='%s']" % path
	page.hover(row)
	page.click(row + " button[aria-label='Stage %s']" % path)
	page.wait_for_selector(row + " button[aria-label='Unstage %s']" % path, state="attached")
	page.click(row)
	page.wait_for_selector(".git-diff .dl.add")
	run.shot("staged")
	page.fill(".git-message", "e2e: a commit")
	page.click(".git-composer button:has-text('Commit')")
	page.wait_for_selector(".git-row:has-text('e2e: a commit')")
	page.click(".git-row:has-text('e2e: a commit')")
	page.wait_for_selector(".cmeta .subject:has-text('e2e: a commit')")
	page.wait_for_selector(".cfiles .git-file[data-path='%s']" % path)
	page.wait_for_selector(".git-diff .dl")
	run.shot("details")
	page.click(".git-bar button:has-text('Branch')")
	page.fill(".dialog input", "e2e/branch")
	page.press(".dialog input", "Enter")
	page.wait_for_selector(".git-refs .row.sel-dim:has-text('e2e/branch')")
	page.fill(".git-search", "Fixture" if real else "Ring")
	page.wait_for_function("document.querySelectorAll('.git-row').length === 1")
	run.shot("search")
	repo = run.request("git.repos", {"wsId": page.evaluate("ied_shell.wsId")})["repos"][0]["repoId"]
	run.request("git.checkout", {"repoId": repo, "branch": "main" if real else "port/logging"})


@scenario(viewports=("phone",))
def git_on_a_phone(run):
	page = open_git(run)
	run.shot("graph")
	page.click(".git-row:not(.local) >> nth=0")
	page.wait_for_selector(".git-act.detail-open .cmeta .subject")
	run.shot("details")
	page.click(".git-back")
	page.wait_for_selector(".git-row:not(.local)")


# ---------------------------------------------------------------------------------------------------------------
# White space (editor.renderWhitespace, on by default): View White Space (Ctrl+R, Ctrl+W) turns it off in every open
# editor and records it in the User tier.

@scenario(viewports=("desktop",))
def view_white_space(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	shown = "monaco.editor.getEditors().filter((e) => e.getModel()).map((e) => e.getOption(monaco.editor.EditorOption.renderWhitespace))"
	check(page.evaluate(shown) == ["all"], "white space shows by default: %s" % page.evaluate(shown))
	run.shot("shown")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+r")
	page.keyboard.press("Control+w")
	page.wait_for_function(shown + ".every((option) => option === 'none')")
	values = run.request("settings.get", {"wsId": page.evaluate("ied_shell.wsId")})["values"]
	check(values["editor.renderWhitespace"] == {"value": "none", "source": "app"}, "recorded in the User tier: %s" % values["editor.renderWhitespace"])
	run.shot("hidden")
	run.request("settings.reset", {"tier": "app", "key": "editor.renderWhitespace"})
	page.wait_for_function(shown + ".every((option) => option === 'all')")


# ---------------------------------------------------------------------------------------------------------------
# Keyboard (docs/15 "The editor's keys", docs/07 "Host settings"): Visual Studio's editor keys, and the owner's own
# shortcuts from Settings > Environment > Keyboard, which win and can take a default away.

def take_lease(page):
	"""Edit Here when another window (an earlier scenario's) holds the document."""
	if page.is_visible(".ed-bar button:has-text('Edit Here')"):
		page.click(".ed-bar button:has-text('Edit Here')")
		page.wait_for_selector(".ed-bar button:has-text('Edit Here')", state="hidden")


def badge(page, name):
	"""The class, label and hue of a file's glyph in the Solution Explorer."""
	return page.evaluate("""(name) => {
		const row = [...document.querySelectorAll('.tree-row')].find((r) => r.querySelector('.name') && r.querySelector('.name').textContent === name);
		const svg = row && row.querySelector('svg.fg');
		return svg ? [svg.getAttribute('class'), svg.querySelector('text') ? svg.querySelector('text').textContent : null, svg.style.getPropertyValue('--fg-hue')] : null;
	}""", name)


def expand(page, name):
	"""Opens a folder of the Solution Explorer (its glyph says whether it is)."""
	row = ".tree-row:has(.name:text-is('%s'))" % name
	page.wait_for_selector(row + " svg.fg")
	if page.query_selector(row + " svg.fg-folder"):
		page.click(row)
	page.wait_for_selector(row + " svg.fg-folder-open")


@scenario(viewports=("desktop", "phone"))
def file_icons_from_plugins(run):
	"""Files, folders and tabs show the icons the language plugins describe (docs/15 "Icons"): a C source "C", its
	header "h", Markdown gray, a picture's own codicon in its hue, folders tan; the shell knows none of them."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	made = ["docs/icons-%s.%s" % (run.viewport, ext) for ext in ("c", "h", "png")]
	for path in made:
		run.request("fs.write", {"wsId": ws, "path": path, "text": ""})
	source, header, picture = (path.split("/")[1] for path in made)
	if run.phone:
		page.evaluate("ied_shell.showView('ied.explorer')")
	page.wait_for_selector(".tree-row:has(.name:text-is('README.md')) svg.fg-file")
	check(badge(page, "README.md")[1:] == ["MD", "var(--hue-gray)"], "Markdown: %s" % badge(page, "README.md"))
	expand(page, "docs")
	page.wait_for_selector(".tree-row:has(.name:text-is('%s')) svg.fg-file" % header)
	check(badge(page, source)[1:] == ["C", "var(--hue-blue)"], "a C source: %s" % badge(page, source))
	check(badge(page, header)[1:] == ["h", "var(--hue-violet)"], "its header: %s" % badge(page, header))
	color = page.evaluate("(name) => [...document.querySelectorAll('.tree-row')].find((r) => r.textContent.includes(name)).querySelector('.ci').style.color", picture)
	check(color == "var(--hue-teal)", "a picture: the plugin's codicon in its hue, %s" % color)
	run.shot("explorer")
	page.dblclick(".tree-row:has(.name:text-is('%s'))" % source)
	page.wait_for_selector(".phone-head svg.fg-file" if run.phone else ".tab:has-text('%s') svg.fg-file text:text-is('C')" % source)
	run.shot("tab")
	for path in made:
		run.request("fs.delete", {"wsId": ws, "path": path})


def line_count(page):
	return page.evaluate("monaco.editor.getEditors().find((e) => e.getModel() && e.getModel().uri.toString().endsWith('README.md')).getModel().getLineCount()")


@scenario(viewports=("desktop",))
def editor_keys_and_keyboard_page(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	page.click(".monaco-editor .view-lines")
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'README.md', line: 0, col: 0 })")
	take_lease(page)
	page.click(".monaco-editor .view-lines")
	before = line_count(page)
	page.keyboard.press("Control+d")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before + 1)
	page.keyboard.press("Control+Shift+l")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before)
	page.keyboard.press("Control+z")
	page.keyboard.press("Control+z")
	page.evaluate("ied_shell.commands.run('shell.keyboardShortcuts')")
	page.wait_for_selector(".kb-row")
	page.fill(".kb-search", "duplicate")
	page.click(".kb-row[data-command='ied.editor.duplicate'] .kb-key[data-chord='ctrl+d'] button")
	page.wait_for_selector(".kb-row[data-command='ied.editor.duplicate'] .kb-key.removed")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Add')")
	page.wait_for_selector(".kb-dialog")
	page.keyboard.press("Control+k")
	page.keyboard.press("Control+d")
	run.wait_text(".kb-dialog .kb-recorded", "Ctrl+K, Ctrl+D")
	check("Edit: Format Document" in page.inner_text(".kb-dialog .kb-clash"), "the dialog names the command that has the key")
	run.shot("record")
	page.keyboard.press("Escape")
	page.wait_for_selector(".kb-dialog", state="detached")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Add')")
	page.keyboard.press("Control+Shift+d")
	page.keyboard.press("Enter")
	page.wait_for_selector(".kb-row[data-command='ied.editor.duplicate'] .kb-key.user[data-chord='ctrl+shift+d']")
	run.shot("changed")
	values = run.request("settings.get", {})["values"]["keyboard.bindings"]["value"]
	check(values == [{"command": "ied.editor.duplicate", "key": "ctrl+d", "remove": True}, {"command": "ied.editor.duplicate", "key": "ctrl+shift+d", "when": "activeTabKind == 'doc'"}], "keyboard.bindings: %s" % values)
	page.click(".tab:has-text('README.md')")
	take_lease(page)
	page.click(".monaco-editor .view-lines")
	before = line_count(page)
	page.keyboard.press("Control+Shift+d")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before + 1)
	page.keyboard.press("Control+z")
	page.evaluate("ied_shell.commands.run('shell.keyboardShortcuts')")
	page.fill(".kb-search", "duplicate")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Reset')")
	page.wait_for_function("ied_shell.keybindings.labelFor('ied.editor.duplicate') === 'Ctrl+D'")


# ---------------------------------------------------------------------------------------------------------------
# The browser (docs/27)

BROWSER_DEMO = """<!doctype html>
<html><head><title>E2E page</title></head>
<body style="margin:0;font:16px sans-serif">
<input id="field" aria-label="Field" style="position:absolute;left:20px;top:60px;width:200px;height:30px">
<button id="go" style="position:absolute;left:20px;top:120px;width:120px;height:40px" onclick="document.title = 'Went ' + document.getElementById('field').value">Go</button>
<script>
	document.getElementById('field').addEventListener('keydown', (e) => { if (e.key === 'Enter') document.title = 'Got ' + e.target.value; });
	console.error('e2e boom');
</script>
</body></html>
"""


@scenario(viewports=("desktop", "phone"), targets=("real",))
def browser_tab(run):
	"""A web page that lives on the server, in a Browser tab (docs/27): View in Browser shows a workspace file, a click and
	typed keys reach the page, its console lists the page's error, and what the AI does in the page is drawn over it."""
	site = os.path.join(run.target.workspace, "site")
	os.makedirs(site, exist_ok=True)
	with open(os.path.join(site, "demo.html"), "w", encoding="utf-8") as f:
		f.write(BROWSER_DEMO)
	page = run.open()
	page.evaluate("() => ied_shell.commands.run('ied.browser.openFile', { path: 'site/demo.html' })")
	page.wait_for_selector(".browser-view .browser-frame:not([hidden])", timeout=30000)
	titles = ".phone-head" if run.phone else ".tab .name"
	run.wait_text(titles, "E2E page", timeout=30000)
	frame = page.query_selector(".browser-frame").bounding_box()
	page.mouse.click(frame["x"] + 60, frame["y"] + 75)
	page.keyboard.type("hello")
	page.keyboard.press("Enter")
	run.wait_text(titles, "Got hello", timeout=20000)
	run.shot("typed")
	page.click(".browser-toolbar button[aria-label='Console']")
	run.wait_text(".browser-console-list", "e2e boom", timeout=20000)
	run.shot("console")
	ws = page.evaluate("() => ied_shell.wsId")
	tab = page.evaluate("() => ied_shell.context.get('activeTab')") or ""
	pages = run.request("browser.pages", {"wsId": ws})["pages"]
	demo = [p["pageId"] for p in pages if "browser:" + p["pageId"] == tab and p["url"].endswith("/site/demo.html")]
	check(len(demo) == 1, "the tab's page (%s) is the workspace's: %s" % (tab, pages))
	tree = run.request("plugins.call", {"name": "browser.snapshot", "args": {"wsId": ws, "args": {"page": demo[0]}}})["result"]["text"]
	go = [line for line in tree.splitlines() if 'button "Go"' in line]
	check(go and "[ref=" in go[0], "the snapshot names the button with a ref: %s" % tree)
	ref = go[0].split("[ref=")[1].rstrip("]")
	run.request("plugins.call", {"name": "browser.act", "args": {"wsId": ws, "args": {"page": demo[0], "action": "click", "ref": ref}}})
	page.wait_for_selector(".browser-box:not([hidden])", timeout=10000)
	run.wait_text(".browser-caption", 'AI: click button "Go"')
	run.shot("ai-action")
	run.wait_text(titles, "Went hello", timeout=20000)


import profiler_scenarios  # noqa: E402,F401  (the profiler's scenarios register themselves)


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def start_page_and_recent_projects(run):
	"""IED's start page (`#start`, what the desktop app opens without a path; docs/15 "Start page"): the open workspaces,
	the favorites and the recent projects; a star moves a project between them, a click opens one, File > Start Page
	comes back, and the workspace switcher lists the same. The mock's list: demo (open), Tools, Shooter (a favorite) and
	Prototype (gone). Each viewport leaves it as it found it, but for Tools, which stays open."""
	page = run.open()
	page.goto(run.target.url + "/#start")
	page.wait_for_function("window.ied_shell && ied_shell.client && ied_shell.client.state === 'live' && document.querySelectorAll('.start-page .sp-row').length >= 4", timeout=20000)

	def names(section):
		return page.eval_on_selector_all(".sp-section", "(els, title) => { const s = els.find(e => e.querySelector('h3').textContent === title); return s ? [...s.querySelectorAll('.sp-name')].map(n => n.textContent) : []; }", section)

	def row(path):
		return ".sp-row[data-path='%s']" % path

	check("demo" in names("Open"), "the open workspace")
	check(names("Favorites") == ["Shooter"], "the favorite")
	check(page.eval_on_selector(row("C:/mock/Prototype"), "e => e.classList.contains('gone') && e.querySelector('.sp-when').textContent") == "not found", "a gone folder says so")
	run.shot("start")
	page.click(row("C:/mock/Shooter") + " button[aria-label='Remove from Favorites']")
	page.wait_for_function("() => [...document.querySelectorAll('.sp-section')].some(s => s.querySelector('h3').textContent === 'Recent' && s.textContent.includes('Shooter'))")
	page.hover(row("C:/mock/Shooter"))
	page.click(row("C:/mock/Shooter") + " button[aria-label='Add to Favorites']")
	page.wait_for_function("() => [...document.querySelectorAll('.sp-section')].some(s => s.querySelector('h3').textContent === 'Favorites' && s.textContent.includes('Shooter'))")
	tools = row("C:/mock/Tools/Tools.iedsln") if page.query_selector(row("C:/mock/Tools/Tools.iedsln")) else row("C:/mock/Tools")
	page.click(tools)
	page.wait_for_function("document.title === 'Tools - IED' && !document.querySelector('.start-page')", timeout=20000)
	check(page.evaluate("location.hash") == "", "a project chosen, the start page is no longer asked for")
	page.click("button[aria-label='Switch workspace']")
	page.wait_for_selector(".menu .menu-item:has-text('Start Page')")
	labels = run.texts(".menu .menu-item .label")
	check("Shooter" in labels and "demo" in labels and "Prototype" in labels, "the switcher lists the same projects: %r" % labels)
	run.shot("switcher")
	page.click(".menu .menu-item:has-text('Start Page')")
	page.wait_for_function("() => location.hash === '#start' && [...document.querySelectorAll('.sp-section')].some(s => s.querySelector('h3').textContent === 'Open' && s.textContent.includes('Tools'))", timeout=10000)


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def solution_explorer_switches_views(run):
	"""The Solution Explorer's Switch Views (docs/15): a solution shows its projects and Solution Items, or its folder,
	and back. The mock's recent solution Tools opens as a solution with one project."""
	page = run.open()
	ws = run.request("ws.openRecent", {"path": "C:/mock/Tools/Tools.iedsln"})["workspace"]["wsId"]
	page.evaluate("(id) => ied_shell.showWorkspace(id)", ws)
	page.wait_for_function("document.title === 'Tools - IED' && ied_shell.layout", timeout=20000)
	run.show_activity("ied.editor")
	if run.phone:
		page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.explorer' })")
	page.wait_for_selector(".explorer .tree-row:has-text(\"Solution 'Tools' (1 project)\")")
	page.click(".explorer .tree-row:has-text(\"Solution 'Tools'\")")
	page.keyboard.press("ArrowRight")
	page.wait_for_selector(".explorer .tree-row:has-text('Solution Items')")
	run.shot("solution")
	page.click("button[aria-label='Switch to Folder View']")
	page.wait_for_selector(".explorer .tree-row:has-text('Tools.iedsln')")
	check(not page.query_selector(".explorer .tree-row:has-text(\"Solution 'Tools'\")"), "the folder instead of the solution")
	rows = run.texts(".explorer .tree-row")
	check(all(any(name in row for row in rows) for name in ("src", "README.md", "Tools.iedsln")), "the folder's entries: %r" % rows)
	run.shot("folder")
	page.click("button[aria-label='Switch to Solution View']")
	page.wait_for_selector(".explorer .tree-row:has-text(\"Solution 'Tools' (1 project)\")")
