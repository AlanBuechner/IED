"""Finding files by name (docs/10 "Files", fs.files): the query's characters in order in the path, any case, a match in
the file's own name before one in its folders, shorter paths first on a tie, at most maxResults."""

from harness import ProtocolTest
from iedclient import new_id


class FileFinderTests(ProtocolTest):
	async def test_files_by_name(self):
		client = await self.client()
		ws = (await client.request("testhooks.scratch", {"lines": 1}))["wsId"]
		token = new_id("Finder").replace("-", "")
		paths = ["%s/%s.c" % (token, token), "%s/deep/inner/%s.h" % (token, token), "%s/%sNotes/readme.md" % (token, token)]
		for path in paths:
			await client.request("fs.write", {"wsId": ws, "path": path, "text": "x\n"})
		found = [f["path"] for f in (await client.request("fs.files", {"wsId": ws, "query": token.lower()}))["files"]]
		self.assertEqual(paths, found[:3], "in the name before in a folder, the shorter path first")
		one = (await client.request("fs.files", {"wsId": ws, "query": token, "maxResults": 1}))["files"]
		self.assertEqual([paths[0]], [f["path"] for f in one])
		self.assertEqual([], (await client.request("fs.files", {"wsId": ws, "query": token + "qqq"}))["files"])
