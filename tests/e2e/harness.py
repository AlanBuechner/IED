"""What a scenario works with: a page of the shell at a viewport on a target, screenshots, server calls through the
page's own protocol client, and the console errors to fail on."""

import os
import time

VIEWPORTS = {
	"desktop": {"viewport": {"width": 1920, "height": 1080}, "device_scale_factor": 1},
	"laptop": {"viewport": {"width": 1366, "height": 768}, "device_scale_factor": 1},
	"tablet": {"viewport": {"width": 1024, "height": 768}, "device_scale_factor": 1},
	"phone": {"viewport": {"width": 390, "height": 844}, "device_scale_factor": 2, "is_mobile": True, "has_touch": True},
}

RESET_LAYOUTS = """async (classes) => {
	for (const deviceClass of classes) {
		let revision = 0;
		for (let attempt = 0; attempt < 4; attempt++) {
			try {
				await ied_shell.client.request("layout.set", { wsId: ied_shell.wsId, deviceClass, layout: null, expectedRevision: revision });
				break;
			}
			catch (e) {
				if (e.code !== "conflict") throw e;
				revision = e.data.revision;
			}
		}
	}
	for (const note of ied_shell.server.notifications) await ied_shell.client.request("server.dismiss", { id: note.id });
}"""


class ScenarioFailure(AssertionError):
	pass


def check(condition, message):
	if not condition:
		raise ScenarioFailure(message)


class Run:
	def __init__(self, target, browser, viewport, shots, scenario):
		self.target = target
		self.browser = browser
		self.viewport = viewport
		self.shots = shots
		self.scenario = scenario
		self.contexts = []
		self.errors = []
		self.page = None

	@property
	def phone(self):
		return self.viewport == "phone"

	def open(self, fresh=True):
		"""A new page of the shell; with `fresh`, every layout of the workspace is forgotten first."""
		# Playwright's wait_for_function evaluates its predicate as a string; the real server's CSP (docs/11, checked by
		# tools/server-checks) forbids that, so test contexts bypass it.
		context = self.target.new_context(self.browser, bypass_csp=True, **VIEWPORTS[self.viewport])
		self.contexts.append(context)
		page = context.new_page()
		page.on("pageerror", lambda e: self.errors.append("page error: %s" % e))
		page.on("console", lambda m: self.errors.append("console %s: %s" % (m.type, m.text)) if m.type == "error" else None)
		self.page = page
		self.goto()
		if fresh:
			page.evaluate(RESET_LAYOUTS, ["desktop", "tablet", "phone"])
			self.reload()
		return page

	def goto(self):
		self.page.goto(self.target.url + "/")
		self.wait_ready()

	def reload(self):
		self.page.reload()
		self.wait_ready()

	def wait_ready(self):
		self.page.wait_for_function("document.documentElement.dataset.ready === '1' && window.ied_shell && ied_shell.layout && ied_shell.client.state === 'live'", timeout=20000)
		self.page.wait_for_selector(".phone-nav .pn" if self.phone else ".dock-group", timeout=20000)
		time.sleep(0.3)

	def request(self, method, params=None):
		return self.page.evaluate("([method, params]) => ied_shell.client.request(method, params)", [method, params or {}])

	def shot(self, name):
		folder = os.path.join(self.shots, self.target.kind + "-" + self.viewport)
		os.makedirs(folder, exist_ok=True)
		self.page.screenshot(path=os.path.join(folder, self.scenario + "-" + name + ".png"))

	def texts(self, selector):
		return self.page.eval_on_selector_all(selector, "els => els.map(e => e.textContent.trim())")

	def wait_text(self, selector, text, timeout=10000):
		self.page.wait_for_function("([sel, text]) => [...document.querySelectorAll(sel)].some(e => e.textContent.includes(text))", arg=[selector, text], timeout=timeout)

	def close(self):
		for context in self.contexts:
			context.close()
