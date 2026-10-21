"""Search in files (docs/10 "Files", fs.search): the first match of each line with its UTF-16 column, literals and
regexes, case, includes and the default exclusions, a limit, and open documents searched as they are."""

from harness import ProtocolTest
from iedclient import RpcFailure, new_id


class SearchTests(ProtocolTest):
	async def setup_files(self, client):
		ws = (await client.request("testhooks.scratch", {"lines": 1}))["wsId"]
		token = new_id("needle").replace("-", "")
		folder = "search-" + token
		files = {
			folder + "/a.txt": "x %s y %s\né%s\n" % (token, token, token),
			folder + "/b.md": "nothing here\n",
			folder + "/sub/c.txt": "%s\n" % token.upper(),
			folder + "/node_modules/d.txt": "%s\n" % token,
		}
		for path, text in files.items():
			await client.request("fs.write", {"wsId": ws, "path": path, "text": text})
		return ws, token, folder

	async def search(self, client, ws, **params):
		return await client.request("fs.search", dict(params, wsId=ws))

	async def test_matches_lines_columns_and_case(self):
		client = await self.client()
		ws, token, folder = await self.setup_files(client)
		found = await self.search(client, ws, query=token)
		self.assertFalse(found["truncated"])
		where = [(m["path"], m["line"], m["col"]) for m in found["matches"]]
		self.assertEqual([(folder + "/a.txt", 0, 2), (folder + "/a.txt", 1, 1), (folder + "/sub/c.txt", 0, 0)], where,
			"one match per line, the column in UTF-16 units, any case, node_modules skipped")
		self.assertEqual("x %s y %s" % (token, token), found["matches"][0]["preview"])
		cased = await self.search(client, ws, query=token, caseSensitive=True)
		self.assertEqual(2, len(cased["matches"]))

	async def test_regex_include_exclude_and_limit(self):
		client = await self.client()
		ws, token, folder = await self.setup_files(client)
		regex = await self.search(client, ws, query="^" + token, regex=True, caseSensitive=True)
		self.assertEqual([], regex["matches"], "anchored: no line starts with it in that case")
		only = await self.search(client, ws, query=token, include=["**/sub/*.txt"])
		self.assertEqual([folder + "/sub/c.txt"], [m["path"] for m in only["matches"]])
		everything = await self.search(client, ws, query=token, exclude=[])
		self.assertIn(folder + "/node_modules/d.txt", [m["path"] for m in everything["matches"]], "no exclusions: every folder")
		limited = await self.search(client, ws, query=token, maxResults=1)
		self.assertEqual((1, True), (len(limited["matches"]), limited["truncated"]))

	async def test_open_documents_are_searched_as_they_are(self):
		client = await self.client()
		ws, token, folder = await self.setup_files(client)
		doc = await client.request("doc.open", {"wsId": ws, "path": folder + "/b.md"})
		fresh = new_id("unsaved").replace("-", "")
		change = {"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": fresh + " "}
		await client.request("doc.edit", {"docId": doc["docId"], "baseVersion": doc["version"], "changes": [change]})
		found = await self.search(client, ws, query=fresh)
		self.assertEqual([(folder + "/b.md", 0, 0)], [(m["path"], m["line"], m["col"]) for m in found["matches"]])

	async def test_refusals(self):
		client = await self.client()
		ws, _, _ = await self.setup_files(client)
		for params in ({"query": ""}, {"query": "(", "regex": True}):
			with self.assertRaises(RpcFailure) as refused:
				await self.search(client, ws, **params)
			self.assertEqual("bad_request", refused.exception.code)
