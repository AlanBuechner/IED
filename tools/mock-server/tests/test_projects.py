"""The mock's project management (docs/03 "Managing projects"): project.add makes the demo folder a solution in place
and adds folders as projects, project.move moves one with its open documents, project.remove takes one out (its
files kept, or its folder in the trash); the refusals are the server's."""

from mockcase import MockCase, RpcFailure


class ProjectTests(MockCase):
	async def asyncSetUp(self):
		await super().asyncSetUp()
		self.c = await self.client()
		await self.c.sub(["ws:w1", "server"])
		self.c.drain("ws:w1")
		self.c.drain("server")

	async def changed(self):
		return (await self.c.wait_for("ws:w1", lambda f: f.get("kind") == "solution.changed"))[-1]["data"]

	async def refused(self, code, text, method, params):
		with self.assertRaises(RpcFailure) as failure:
			await self.c.request(method, params)
		self.assertEqual(code, failure.exception.code, str(failure.exception))
		self.assertIn(text, str(failure.exception))

	async def test_a_folder_becomes_a_solution_and_its_projects_change(self):
		added = await self.c.request("project.add", {"wsId": "w1", "folder": "src/game"})
		self.assertEqual(("game", "solution"), (added["project"], added["workspace"]["kind"]))
		changed = await self.changed()
		self.assertEqual(("solution", ["game"]), (changed["kind"], [p["name"] for p in changed["solution"]["projects"]]))
		summary = (await self.c.wait_for("server", lambda f: f.get("kind") == "workspace.changed"))[-1]["data"]
		self.assertEqual("solution", summary["kind"])
		await self.c.request("project.add", {"wsId": "w1", "folder": "src/engine", "name": "Engine"})
		await self.refused("conflict", "is project game already", "project.add", {"wsId": "w1", "folder": "src/game"})
		await self.refused("not_found", "no folder", "project.add", {"wsId": "w1", "folder": "nowhere"})
		await self.c.request("project.move", {"wsId": "w1", "project": "Engine", "to": "libs/engine"})
		listed = await self.c.request("fs.list", {"wsId": "w1", "path": "libs/engine"})
		self.assertIn("render.c", [e["name"] for e in listed["entries"]])
		await self.refused("bad_request", "into its own folder", "project.move", {"wsId": "w1", "project": "Engine", "to": "libs/engine/inner"})
		kept = await self.c.request("project.remove", {"wsId": "w1", "project": "Engine"})
		self.assertNotIn("trashId", kept)
		deleted = await self.c.request("project.remove", {"wsId": "w1", "project": "game", "deleteFiles": True})
		self.assertTrue(deleted["trashId"])
		await self.refused("not_found", "its projects: none", "project.remove", {"wsId": "w1", "project": "game"})
		self.assertIn("render.c", [e["name"] for e in (await self.c.request("fs.list", {"wsId": "w1", "path": "libs/engine"}))["entries"]], "a removed project keeps its files")
