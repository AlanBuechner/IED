"""What stays in memory of a chat (docs/04 "What stays in memory", docs/10 "Chats"): an idle chat lets its turns go once
ai.chats.offloadAfterMinutes pass, a tab showing it or not - the workspace server's memory drops by what they held, its
summary still counts them - and its tab goes on paging from disk without loading it; its next turn reads them back as
they were. A client gets a long turn a page at a time, never whole. After a restart a chat starts offloaded."""

import asyncio
import ctypes
import ctypes.wintypes
import hashlib
import json
import os
import time

from server import local_state, workspace_log
from test_chat import ChatCase, inline, text_step
from test_servers import workspace_status

MB = 1024 * 1024
BIG = 4 * MB                 # bytes of the long answer: the turn keeps it twice (its text segment, the model's message)
KEEPER_SECONDS = 40          # the keeper looks every 15 s
FIRST_LINE = "First line of a long answer."
PAGE_SEGMENTS = 120          # c_PageSegments (plugins/chat/code/chats/Pages.ch)
LONG_TURN = 300


def long_answer():
	"""BIG bytes no line of which repeats another (the repetition guard cuts a reply that loops, docs/12)."""
	lines = [FIRST_LINE]
	for i in range(BIG // 80):
		lines.append("%07d %s" % (i, hashlib.sha256(str(i).encode()).hexdigest()))
	return "\n".join(lines) + "\n"


class MemoryCounters(ctypes.Structure):
	_fields_ = [("cb", ctypes.wintypes.DWORD), ("PageFaultCount", ctypes.wintypes.DWORD)] + [(name, ctypes.c_size_t) for name in (
		"PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
		"QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


def private_bytes(pid):
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	psapi = ctypes.WinDLL("psapi", use_last_error=True)
	handle = kernel32.OpenProcess(0x1000 | 0x0010, False, pid)
	try:
		counters = MemoryCounters()
		counters.cb = ctypes.sizeof(counters)
		psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
		return counters.PrivateUsage
	finally:
		kernel32.CloseHandle(handle)


class OffloadTests(ChatCase):
	async def private_bytes(self):
		"""The workspace server's private memory once its garbage is collected (string buffers are not on the
		collector's heap, so its live count would not show them)."""
		await self.client.request("testhooks.gc", {"wsId": "w1"})
		return private_bytes(workspace_status(self.server, "w1")["pid"])

	def log(self):
		with open(workspace_log(self.data, self.repo), encoding="utf-8", errors="replace") as f:
			return f.read()

	async def wait_for_log(self, text, timeout=KEEPER_SECONDS, since=0):
		"""The log saying `text` after its first `since` characters; the server writes it a moment after it happens."""
		deadline = time.time() + timeout
		while text not in self.log()[since:]:
			self.assertLess(time.time(), deadline, "the workspace server's log never said %r" % text)
			await asyncio.sleep(0.5)

	async def opened(self, topic):
		"""The chat's snapshot as a client opening it gets it (events queued before it are passed over)."""
		await self.client.sub([topic])
		return (await self.client.wait_for(topic, lambda f: f["t"] == "snap"))[-1]["data"]

	async def offload_soon(self):
		await self.client.request("settings.set", {"tier": "app", "key": "ai.chats.offloadAfterMinutes", "value": 0.005})

	async def test_an_idle_chat_lets_its_turns_go_while_its_tab_pages_from_disk(self):
		chat_id, topic = await self.chat()
		self.model.queue(inline({"text": long_answer(), "chunk": 65536, "delayMs": 0}, {"finish": "stop"}))
		self.assertEqual("finished", (await self.turn(chat_id, "Write a lot"))["stop"])
		loaded = await self.private_bytes()
		await self.offload_soon()
		await self.wait_for_log("chat %s offloaded: 1 turn" % chat_id)
		offloaded = await self.private_bytes()
		self.assertGreater(loaded - offloaded, BIG, "its turns and the events its topic kept let go, its tab open: %.1f MB private before, %.1f MB after" % (loaded / MB, offloaded / MB))
		summary = await self.summary(chat_id)
		self.assertEqual((1, "finished"), (summary["turnCount"], summary["lastStop"]), "the summary still counts its turns")

		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(long_answer(), self.segments(history[0], "text")[0]["text"], "a page of it read from disk, as it was")
		snapshot = await self.opened(topic)
		self.assertEqual(["Write a lot"], [t["user"]["text"] for t in snapshot["turns"]])
		await asyncio.sleep(1)
		self.assertNotIn("chat %s loaded from disk" % chat_id, self.log(), "paging and opening it load nothing")

		self.model.queue(text_step("Short."))
		self.assertEqual("finished", (await self.turn(chat_id, "And now briefly"))["stop"])
		await self.wait_for_log("chat %s loaded from disk: 1 turn" % chat_id, timeout=5)
		last = await self.client.request("chat.history", {"chatId": chat_id})
		self.assertEqual((["And now briefly"], True), ([t["user"]["text"] for t in last["turns"]], last["hasMore"]), "the 4 MB answer does not fit the last page's room")
		before = await self.client.request("chat.history", {"chatId": chat_id, "before": last["turns"][0]["turnId"]})
		self.assertEqual(["Write a lot"], [t["user"]["text"] for t in before["turns"]], "the page before has it")

	async def test_a_long_turn_comes_a_page_at_a_time(self):
		chat_id, topic = await self.chat()
		self.model.queue(text_step("Ready."))
		await self.turn(chat_id, "One")
		self.client.abort()
		self.server.stop(remove_data=False)
		turn = {"turnId": "u900", "user": {"text": "Long"}, "mode": "ask", "model": "fake", "group": "", "stop": "finished", "recoveries": [], "tokensIn": 0, "tokensOut": 0,
			"startedMs": 1, "endedMs": 2, "segments": [{"segId": "g%d" % i, "type": "text", "text": "segment %d" % i, "done": True} for i in range(LONG_TURN)],
			"messages": [{"role": "user", "content": "Long"}, {"role": "assistant", "content": "ok"}]}
		with open(os.path.join(local_state(self.data, self.repo), "chats", chat_id, "turns.jsonl"), "a", encoding="utf-8", newline="\n") as f:
			f.write(json.dumps(turn) + "\n")
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()

		snapshot = await self.opened(topic)
		last = snapshot["turns"][-1]
		self.assertEqual(("u900", LONG_TURN), (last["turnId"], last["segmentCount"]), "the long turn, cut")
		self.assertLessEqual(sum(len(t["segments"]) for t in snapshot["turns"]), PAGE_SEGMENTS, "a page of it, not all of it")
		self.assertTrue(snapshot["hasMore"])
		seen = [s["segId"] for t in snapshot["turns"] for s in t["segments"]]
		first = snapshot["turns"][0]
		page = snapshot
		while page["hasMore"]:
			page = await self.client.request("chat.history", {"chatId": chat_id, "before": first["turnId"], "beforeSeg": first.get("firstSeg", 0)})
			self.assertLessEqual(sum(len(t["segments"]) for t in page["turns"]), PAGE_SEGMENTS)
			seen = [s["segId"] for t in page["turns"] for s in t["segments"]] + seen
			first = page["turns"][0]
		self.assertEqual(["g%d" % i for i in range(LONG_TURN)], [s for s in seen if s.startswith("g")], "paging back gives every segment once, in order")
		self.assertEqual("One", page["turns"][0]["user"]["text"], "back to the first turn")
		ahead = await self.client.request("chat.history", {"chatId": chat_id, "after": "u900", "afterSeg": 250})
		self.assertEqual(["g%d" % i for i in range(250, LONG_TURN)], [s["segId"] for s in ahead["turns"][0]["segments"]])
		self.assertFalse(ahead["hasNewer"], "the chat's end")

	async def test_history_reads_an_offloaded_chat_and_a_restart_starts_it_offloaded(self):
		chat_id, topic = await self.chat()
		self.model.queue(text_step(FIRST_LINE + "\nMore."))
		await self.turn(chat_id, "One")
		await self.offload_soon()
		await self.client.unsub([topic])
		await self.wait_for_log("chat %s offloaded: 1 turn" % chat_id)
		history = (await self.client.request("chat.history", {"chatId": chat_id}))["turns"]
		self.assertEqual(["One"], [t["user"]["text"] for t in history], "chat.history reads it back")

		self.client.abort()
		self.server.stop(remove_data=False)
		self.server = self.start_server()
		self.assertTrue(self.server.started, self.server.error)
		await self.connect()
		restarted = len(self.log())
		summary = await self.summary(chat_id)
		self.assertEqual((1, "finished"), (summary["turnCount"], summary["lastStop"]), "the header says what its turns are")
		await asyncio.sleep(1)
		self.assertNotIn("chat %s loaded from disk" % chat_id, self.log()[restarted:], "listing it reads no turn")
		snapshot = await self.opened(topic)
		self.assertEqual(["One"], [t["user"]["text"] for t in snapshot["turns"]])
		await asyncio.sleep(1)
		self.assertNotIn("chat %s loaded from disk" % chat_id, self.log()[restarted:], "opening it reads a page, not the chat")
		self.model.queue(text_step("Two."))
		self.assertEqual("finished", (await self.turn(chat_id, "Two"))["stop"])
		sent = [(m["role"], m.get("content")) for m in self.model.requests()[-1]["body"]["messages"] if m["role"] != "system"]
		self.assertEqual(("assistant", FIRST_LINE + "\nMore."), sent[1], "the model gets the turn read back as it was")
		self.assertEqual("user", sent[2][0])
		await self.wait_for_log("chat %s loaded from disk: 1 turn" % chat_id, timeout=5, since=restarted)
