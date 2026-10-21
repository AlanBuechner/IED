"""Scripted chats (docs/10 "Chats"): chats:<wsId> and chat:<chatId> topics with turns, segments, tool states,
approvals, questions, tasks, the queue and the context, played from fixtures/chat_scripts instead of a model."""

import asyncio
import json
import re
import time

from fixtures import chat_scripts
from rpc import RpcError

SNAPSHOT_TURNS = 20
DELTA_SECONDS = 0.03
CHUNK = re.compile(r"\S*\s*")
MODEL = "mock-model@local"
GROUP = "mock-group"
MODES = ("ask", "plan", "agent")
PERMISSIONS = ("plan", "ask", "acceptEdits", "auto")


def now_ms():
	return int(time.time() * 1000)


class Segment:
	def __init__(self, seg_id, seg_type, **extra):
		self.seg_id = seg_id
		self.kind = seg_type
		self.text = ""
		self.args_text = ""
		self.extra = extra
		self.done = False

	def to_dict(self):
		out = dict(self.extra, segId=self.seg_id, type=self.kind, done=self.done)
		if self.kind == "tool":
			out["argsText"] = self.args_text
		else:
			out["text"] = self.text
		return out


class Turn:
	def __init__(self, turn_id, user_text, mode):
		self.turn_id = turn_id
		self.user_text = user_text
		self.mode = mode
		self.segments = []
		self.stop = None
		self.recoveries = []
		self.tokens_in = 0
		self.tokens_out = 0
		self.started_ms = now_ms()
		self.ended_ms = 0
		self.prefill = None   # while the prompt is read: llama.cpp's prompt_progress

	def to_dict(self):
		out = {
			"turnId": self.turn_id, "user": {"text": self.user_text}, "mode": self.mode, "model": MODEL, "group": GROUP, "stop": self.stop,
			"recoveries": list(self.recoveries), "tokensIn": self.tokens_in, "tokensOut": self.tokens_out, "ttftMs": 400, "genMs": 2000,
			"startedMs": self.started_ms, "segments": [s.to_dict() for s in self.segments],
		}
		if self.ended_ms:
			out["endedMs"] = self.ended_ms
		if self.prefill:
			out["prefill"] = dict(self.prefill)
		return out

	def tool(self, call_id):
		return next((s for s in self.segments if s.kind == "tool" and s.extra.get("callId") == call_id), None)


class Waiting:
	"""An approval or a question: answered once, from any device."""

	def __init__(self, data):
		self.data = data
		self.decision = None
		self.by = None
		self.future = asyncio.get_running_loop().create_future()


class Chat:
	def __init__(self, chat_id, ws_id, title, mode):
		self.chat_id = chat_id
		self.ws_id = ws_id
		self.title = title
		self.mode = mode
		self.permission = "plan" if mode == "plan" else "ask"
		self.group = ""
		self.state = "idle"
		self.state_reason = ""
		self.turns = []
		self.tasks = []
		self.queued = []
		self.modified = []
		self.context = None
		self.approvals = {}
		self.question = None
		self.runner = None
		self.stop_reason = "user_cancelled"
		self.counter = 0
		self.updated_ms = now_ms()

	@property
	def topic(self):
		return "chat:" + self.chat_id

	def next_id(self, prefix):
		self.counter += 1
		return "%s%d" % (prefix, self.counter)

	def summary(self):
		done = sum(1 for t in self.tasks if t["status"] in ("done", "skipped"))
		out = {
			"chatId": self.chat_id, "wsId": self.ws_id, "title": self.title, "mode": self.mode, "permission": self.permission, "group": self.group,
			"state": self.state, "turnCount": len(self.turns), "updatedMs": self.updated_ms, "tasksDone": done, "tasksTotal": len(self.tasks),
			"approvalsWaiting": sum(1 for a in self.approvals.values() if a.decision is None),
		}
		if self.state_reason:
			out["stateReason"] = self.state_reason
		return out


class Chats:
	def __init__(self, server):
		self.server = server
		self.items = {}
		ws_id = next(iter(server.workspace.items))
		server.hub.add("chats:" + ws_id, lambda: self.list_snapshot(ws_id))
		self._seed(self._create(ws_id, "Player movement", "agent"))
		self._create(ws_id, "Scratch", "ask")
		server.rpc.register("ai.probe", self.m_ai_probe)
		for name in ("chat.list", "chat.create", "chat.set", "chat.send", "chat.unqueue", "chat.stop", "chat.approve", "chat.answer", "chat.history", "chat.delete"):
			server.rpc.register(name, getattr(self, "m_" + name.replace(".", "_")))

	def _create(self, ws_id, title, mode):
		chat = Chat("c%d" % (len(self.items) + 1), ws_id, title, mode)
		self.items[chat.chat_id] = chat
		self.server.hub.add(chat.topic, lambda: self.snapshot(chat))
		return chat

	def _seed(self, chat):
		for question, answer in chat_scripts.SEED_TURNS:
			turn = Turn(chat.next_id("u"), question, chat.mode)
			segment = Segment(chat.next_id("s"), "text", model=MODEL)
			segment.text, segment.done = answer, True
			turn.segments.append(segment)
			turn.stop, turn.tokens_in, turn.tokens_out, turn.ended_ms = "finished", 1200, len(answer) // 4, now_ms()
			chat.turns.append(turn)

	def list_snapshot(self, ws_id):
		return {"chats": [c.summary() for c in self.items.values() if c.ws_id == ws_id]}

	def snapshot(self, chat):
		recent = chat.turns[-SNAPSHOT_TURNS:]
		return dict(
			chat.summary(), model=MODEL, groupInUse=chat.group or GROUP, tasks=chat.tasks, queued=chat.queued, modified=chat.modified,
			context=chat.context, hasMore=len(chat.turns) > len(recent), turns=[t.to_dict() for t in recent],
			pendingApprovals=[a.data for a in chat.approvals.values() if a.decision is None],
			pendingQuestion=chat.question.data if chat.question and chat.question.decision is None else None,
		)

	def get(self, chat_id):
		chat = self.items.get(chat_id)
		if chat is None:
			raise RpcError("not_found", "no chat %r" % chat_id)
		return chat

	def publish(self, chat, kind, data):
		self.server.hub.publish(chat.topic, kind, data)

	def changed(self, chat):
		chat.updated_ms = now_ms()
		summary = chat.summary()
		self.publish(chat, "chat.changed", summary)
		self.server.hub.publish("chats:" + chat.ws_id, "chat.changed", summary)

	def on_restart(self):
		for chat in self.items.values():
			if chat.runner is not None and chat.state not in ("idle", "paused"):
				chat.stop_reason = "interrupted"
				chat.runner.cancel()

	# --- methods -----------------------------------------------------------------------------------------------------

	async def m_ai_probe(self, call, p):
		"""A canned answer (docs/10 ai.probe): a profile's endpoint on port 9 is unreachable, any other a llama.cpp."""
		profile = p.raw.get("profile") if "profile" in p.raw else {"endpoint": "http://127.0.0.1:8080/v1", "model": p.str("model")}
		if ":9/" in profile.get("endpoint", ""):
			return {"ok": False, "error": "connection refused", "server": "OpenAI-compatible", "models": [], "modelServed": False, "contextWindow": 0, "trainContext": 0, "slots": 0, "ms": 0}
		model = profile.get("model", MODEL)
		return {"ok": True, "server": "llama.cpp", "models": [model], "modelServed": True, "contextWindow": 32768, "trainContext": 131072, "slots": 2, "ms": 4}

	async def m_chat_list(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		return self.list_snapshot(ws_id)

	def _settings(self, chat, p):
		if "title" in p.raw:
			chat.title = p.str("title")
		if "mode" in p.raw:
			if p.str("mode") not in MODES:
				raise RpcError("bad_request", "mode must be ask, plan or agent")
			chat.mode = p.str("mode")
		if "permission" in p.raw:
			if p.str("permission") not in PERMISSIONS:
				raise RpcError("bad_request", "permission must be plan, ask, acceptEdits or auto")
			chat.permission = p.str("permission")
		if "group" in p.raw:
			chat.group = p.str("group")

	async def m_chat_create(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId")).ws_id
		mode = p.str("mode", "agent", required=False)
		if mode not in MODES:
			raise RpcError("bad_request", "mode must be ask, plan or agent")
		chat = self._create(ws_id, "New chat", mode)
		self._settings(chat, p)
		self.server.hub.publish("chats:" + ws_id, "chat.created", chat.summary())
		return chat.summary()

	async def m_chat_set(self, call, p):
		chat = self.get(p.str("chatId"))
		self._settings(chat, p)
		self.changed(chat)
		return chat.summary()

	async def m_chat_history(self, call, p):
		chat = self.get(p.str("chatId"))
		before = p.str("before", None, required=False)
		limit = p.int("limit", SNAPSHOT_TURNS, required=False)
		ids = [t.turn_id for t in chat.turns]
		end = ids.index(before) if before in ids else len(ids)
		start = max(0, end - limit)
		return {"turns": [t.to_dict() for t in chat.turns[start:end]], "hasMore": start > 0}

	async def m_chat_send(self, call, p):
		chat = self.get(p.str("chatId"))
		text = p.str("text")
		if not text.strip():
			raise RpcError("bad_request", "text is empty")
		delivery = p.str("as", "message", required=False)
		if delivery not in ("message", "queue", "inject"):
			raise RpcError("bad_request", "as must be message, queue or inject")
		if chat.runner is not None:
			if delivery == "inject":
				self._steer(chat, text)
				return {"injected": True}
			chat.queued.append(text)
			self.publish(chat, "queue.changed", {"queued": list(chat.queued)})
			return {"queued": len(chat.queued)}
		name = chat_scripts.pick(text, p.str("mockScript", None, required=False))
		script = chat_scripts.SCRIPTS.get(name)
		if script is None:
			raise RpcError("bad_request", "unknown mockScript %r (have: %s)" % (name, ", ".join(chat_scripts.SCRIPTS)))
		return {"turnId": self._start(chat, text, script), "script": name}

	def _start(self, chat, text, script):
		turn = Turn(chat.next_id("u"), text, chat.mode)
		chat.turns.append(turn)
		if chat.title == "New chat":
			chat.title = text.strip().split("\n")[0][:48]
		chat.state, chat.state_reason, chat.stop_reason = "running", "", "user_cancelled"
		chat.runner = asyncio.ensure_future(self._play(chat, turn, script))
		return turn.turn_id

	def _steer(self, chat, text):
		turn = chat.turns[-1]
		segment = Segment(chat.next_id("s"), "steering")
		segment.text, segment.done = text, True
		turn.segments.append(segment)
		self.publish(chat, "segment.start", dict(segment.to_dict(), turnId=turn.turn_id))
		self.publish(chat, "segment.end", {"segId": segment.seg_id, "tokens": len(text) // 4, "ms": 0})

	async def m_chat_unqueue(self, call, p):
		chat = self.get(p.str("chatId"))
		index = p.int("index")
		if index < 0 or index >= len(chat.queued):
			raise RpcError("not_found", "no queued message at that index")
		del chat.queued[index]
		self.publish(chat, "queue.changed", {"queued": list(chat.queued)})
		return {"queued": list(chat.queued)}

	async def m_chat_stop(self, call, p):
		chat = self.get(p.str("chatId"))
		if chat.runner is not None:
			chat.stop_reason = "user_cancelled"
			chat.runner.cancel()
		return {"state": chat.state}

	async def m_chat_approve(self, call, p):
		chat = self.get(p.str("chatId"))
		approval = chat.approvals.get(p.str("approvalId"))
		if approval is None:
			raise RpcError("not_found", "no approval %r is waiting" % p.str("approvalId"))
		decision = p.str("decision")
		if decision not in ("allow", "deny", "chat"):
			raise RpcError("bad_request", "decision must be allow, deny or chat")
		if approval.decision is not None:
			raise RpcError("conflict", "another device answered first", {"decision": approval.decision, "by": approval.by})
		approval.decision, approval.by = decision, call.client_id
		approval.future.set_result((decision, p.str("message", "", required=False)))
		self.publish(chat, "approval.resolved", {"approvalId": approval.data["approvalId"], "decision": decision, "by": call.client_id})
		return {"decision": decision}

	async def m_chat_answer(self, call, p):
		chat = self.get(p.str("chatId"))
		question = chat.question
		if question is None or question.data["questionId"] != p.str("questionId"):
			raise RpcError("not_found", "no question %r is waiting" % p.str("questionId"))
		if question.decision is not None:
			raise RpcError("conflict", "another device answered first")
		question.decision, question.by = p.str("answer"), call.client_id
		question.future.set_result(question.decision)
		self.publish(chat, "question.resolved", {"questionId": question.data["questionId"], "answer": question.decision, "by": call.client_id})
		return {}

	async def m_chat_delete(self, call, p):
		chat = self.get(p.str("chatId"))
		if chat.runner is not None:
			raise RpcError("conflict", "the chat is running a turn: stop it first")
		del self.items[chat.chat_id]
		self.server.hub.remove(chat.topic)
		self.server.hub.publish("chats:" + chat.ws_id, "chat.deleted", {"chatId": chat.chat_id})
		return {}

	# --- playing a script --------------------------------------------------------------------------------------------

	async def _play(self, chat, turn, script):
		turn.tokens_in = 18000 + 40 * len(chat.turns)
		self.publish(chat, "turn.start", {"turnId": turn.turn_id, "mode": turn.mode, "group": GROUP, "model": MODEL, "user": {"text": turn.user_text}, "startedMs": turn.started_ms})
		self.changed(chat)
		paused = False
		try:
			for op in script:
				paused = await self._op(chat, turn, op) or paused
		except asyncio.CancelledError:
			self._close_open_segments(chat, turn)
			self._withdraw(chat)
			self._end(chat, turn, chat.stop_reason, {})
			paused = chat.stop_reason == "interrupted"
		finally:
			chat.runner = None
			chat.question = None
			chat.state = "paused" if paused else "idle"
			chat.state_reason = turn.stop if paused else ""
			self.changed(chat)
		if chat.queued and turn.stop == "finished":
			text = chat.queued.pop(0)
			self.publish(chat, "queue.changed", {"queued": list(chat.queued)})
			self._start(chat, text, chat_scripts.SCRIPTS[chat_scripts.pick(text, None)])

	async def _op(self, chat, turn, op):
		kind = op[0]
		if kind in ("thinking", "text"):
			await self._stream_segment(chat, turn, Segment(chat.next_id("s"), kind, model=MODEL), op[1], "text")
		elif kind == "tool":
			await self._tool(chat, turn, op[1])
		elif kind == "ask":
			await self._ask(chat, turn, op[1])
		elif kind == "tasks":
			chat.tasks = op[1]
			self.publish(chat, "tasks.changed", {"items": op[1]})
			self.changed(chat)
		elif kind == "harness":
			segment = Segment(chat.next_id("s"), "harness", kind=op[1])
			segment.text, segment.done = op[2], True
			turn.segments.append(segment)
			self.publish(chat, "segment.start", dict(segment.to_dict(), turnId=turn.turn_id))
			self.publish(chat, "segment.end", {"segId": segment.seg_id, "tokens": 0, "ms": 0})
		elif kind == "context":
			chat.context = op[1]
			self.publish(chat, "context.changed", op[1])
		elif kind == "pause":
			await self.server.clock.sleep(op[1])
		elif kind == "progress":
			await self._prefill(chat, turn, *op[1:])
		elif kind == "estimate":
			await self._estimate(chat, turn, *op[1:])
		elif kind == "end":
			extra = dict(op[2])
			paused = extra.pop("paused", False)
			self._end(chat, turn, op[1], extra)
			return paused
		return False

	async def _prefill(self, chat, turn, total, steps, seconds):
		"""The prompt read in `steps` reports over `seconds` (llama.cpp's prompt_progress), then cleared as a reply starts."""
		for step in range(1, steps + 1):
			turn.prefill = {"total": total, "cache": 0, "processed": total * step // steps, "time_ms": int(seconds * 1000 * step / steps)}
			self.publish(chat, "turn.progress", {"turnId": turn.turn_id, "prefill": dict(turn.prefill)})
			await self.server.clock.sleep(seconds / steps)
		turn.prefill = None
		self.publish(chat, "turn.progress", {"turnId": turn.turn_id, "prefill": None})

	async def _estimate(self, chat, turn, total, seconds):
		"""A server that does not report its reading: the estimate once, then cleared as a reply starts."""
		turn.prefill = {"estimated": True, "total": total, "estimatedMs": int(seconds * 1000), "startedMs": now_ms()}
		self.publish(chat, "turn.progress", {"turnId": turn.turn_id, "prefill": dict(turn.prefill)})
		await self.server.clock.sleep(seconds)
		turn.prefill = None
		self.publish(chat, "turn.progress", {"turnId": turn.turn_id, "prefill": None})

	async def _stream_segment(self, chat, turn, segment, text, field):
		turn.segments.append(segment)
		self.publish(chat, "segment.start", dict(segment.to_dict(), turnId=turn.turn_id))
		for chunk in _chunks(text):
			await self.server.clock.sleep(DELTA_SECONDS)
			if field == "text":
				segment.text += chunk
			else:
				segment.args_text += chunk
			self.publish(chat, "segment.delta", {"segId": segment.seg_id, field: chunk})
		self._end_segment(chat, turn, segment)

	def _end_segment(self, chat, turn, segment):
		segment.done = True
		tokens = max(1, len(segment.text or segment.args_text) // 4)
		turn.tokens_out += tokens
		self.publish(chat, "segment.end", {"segId": segment.seg_id, "tokens": tokens, "ms": int(tokens * DELTA_SECONDS * 1000)})

	def _close_open_segments(self, chat, turn):
		for segment in turn.segments:
			if not segment.done:
				self._end_segment(chat, turn, segment)

	def _tool_state(self, chat, segment, state, **extra):
		segment.extra["state"] = state
		segment.extra.update(extra)
		self.publish(chat, "tool.state", dict(extra, callId=segment.extra["callId"], state=state))

	async def _tool(self, chat, turn, spec):
		call_id = chat.next_id("k")
		segment = Segment(chat.next_id("s"), "tool", tool=spec["name"], callId=call_id, state="streaming")
		await self._stream_segment(chat, turn, segment, json.dumps(spec["args"]), "argsText")
		if spec.get("approval") and not await self._approve(chat, segment, spec):
			self._tool_state(chat, segment, "denied", result={"preview": "The user denied %s." % spec["name"], "isError": True})
			return
		self._tool_state(chat, segment, "running")
		await self.server.clock.sleep(spec.get("ms", 50) / 1000.0)
		result = dict({"isError": False}, **spec["result"])
		extra = {"ms": spec.get("ms", 50), "source": spec.get("source", "built-in"), "result": result}
		for key in ("plus", "minus"):
			if spec.get(key):
				extra[key] = spec[key]
		self._tool_state(chat, segment, "error" if result["isError"] else "ok", **extra)
		if spec.get("path"):
			chat.modified = [f for f in chat.modified if f["path"] != spec["path"]] + [{"path": spec["path"], "plus": spec.get("plus", 0), "minus": spec.get("minus", 0)}]
			self.publish(chat, "files.changed", {"files": chat.modified})

	def _withdraw(self, chat):
		for approval in chat.approvals.values():
			if approval.decision is None:
				approval.decision = "withdrawn"
				self.publish(chat, "approval.resolved", {"approvalId": approval.data["approvalId"], "decision": "withdrawn"})

	async def _approve(self, chat, segment, spec):
		data = dict(spec["approval"], approvalId=chat.next_id("a"), callId=segment.extra["callId"], tool=spec["name"], args=spec["args"])
		approval = Waiting(data)
		chat.approvals[data["approvalId"]] = approval
		chat.state = "awaiting_approval"
		self._tool_state(chat, segment, "awaiting_approval")
		self.publish(chat, "approval.request", data)
		self.changed(chat)
		decision, _ = await approval.future
		chat.state = "running"
		self.changed(chat)
		return decision in ("allow", "chat")

	async def _ask(self, chat, turn, spec):
		call_id = chat.next_id("k")
		segment = Segment(chat.next_id("s"), "tool", tool="ask_user", callId=call_id, state="streaming")
		await self._stream_segment(chat, turn, segment, json.dumps(spec), "argsText")
		data = dict(spec, questionId=chat.next_id("q"), callId=call_id)
		chat.question = Waiting(data)
		chat.state = "awaiting_answer"
		self._tool_state(chat, segment, "awaiting_answer")
		self.publish(chat, "question.request", data)
		self.changed(chat)
		answer = await chat.question.future
		chat.question = None
		chat.state = "running"
		self.changed(chat)
		self._tool_state(chat, segment, "ok", ms=0, source="built-in", result={"preview": answer, "isError": False})

	def _end(self, chat, turn, stop, extra):
		turn.stop = stop
		turn.recoveries = list(extra.get("recoveries", []))
		turn.ended_ms = now_ms()
		self.publish(chat, "turn.stats", {"turnId": turn.turn_id, "tokensIn": turn.tokens_in, "tokensOut": turn.tokens_out, "ttftMs": 400, "genMs": 2000})
		self.publish(chat, "turn.end", {"turnId": turn.turn_id, "stop": stop, "recoveries": turn.recoveries, "tokensIn": turn.tokens_in,
			"tokensOut": turn.tokens_out, "ttftMs": 400, "genMs": 2000, "endedMs": turn.ended_ms})


def _chunks(text):
	pieces = [m.group(0) for m in CHUNK.finditer(text) if m.group(0)]
	return ["".join(pieces[i:i + 2]) for i in range(0, len(pieces), 2)]
