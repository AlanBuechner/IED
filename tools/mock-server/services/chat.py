"""Scripted chats: 'chat:<chatId>' topics streaming turns, segments, tool states and approvals (docs/10, docs/12)."""

import asyncio
import json
import re

from fixtures import chat_scripts
from rpc import RpcError

SNAPSHOT_TURNS = 20
DELTA_SECONDS = 0.03
CHUNK = re.compile(r"\S*\s*")
MODEL = "mock-model@local"


class Segment:
	def __init__(self, seg_id, kind, **extra):
		self.seg_id = seg_id
		self.kind = kind
		self.text = ""
		self.extra = extra
		self.done = False

	def to_dict(self):
		out = dict(self.extra, segId=self.seg_id, type=self.kind, done=self.done)
		out["argsText" if self.kind == "tool" else "text"] = self.text
		return out


class Turn:
	def __init__(self, turn_id, user_text, mode):
		self.turn_id = turn_id
		self.user_text = user_text
		self.mode = mode
		self.segments = []
		self.stop = None
		self.tokens_in = 0
		self.tokens_out = 0
		self.extra = {}

	def to_dict(self):
		out = {
			"turnId": self.turn_id, "user": {"text": self.user_text}, "mode": self.mode, "stop": self.stop,
			"tokensIn": self.tokens_in, "tokensOut": self.tokens_out, "segments": [s.to_dict() for s in self.segments],
		}
		out.update(self.extra)
		return out


class Approval:
	def __init__(self, approval_id, call_id, tool, args):
		self.approval_id = approval_id
		self.call_id = call_id
		self.tool = tool
		self.args = args
		self.decision = None
		self.by = None
		self.future = asyncio.get_running_loop().create_future()

	def to_dict(self):
		return {"approvalId": self.approval_id, "callId": self.call_id, "tool": self.tool, "args": self.args}


class Chat:
	def __init__(self, chat_id, ws_id, title, mode):
		self.chat_id = chat_id
		self.ws_id = ws_id
		self.title = title
		self.mode = mode
		self.turns = []
		self.tasks = []
		self.state = "idle"
		self.runner = None
		self.stop_reason = "user_cancelled"
		self.approvals = {}
		self.counter = 0

	@property
	def topic(self):
		return "chat:" + self.chat_id

	def next_id(self, prefix):
		self.counter += 1
		return "%s%d" % (prefix, self.counter)

	def summary(self):
		return {"chatId": self.chat_id, "wsId": self.ws_id, "title": self.title, "mode": self.mode, "state": self.state, "turnCount": len(self.turns)}


class Chats:
	def __init__(self, server):
		self.server = server
		self.items = {}
		ws_id = next(iter(server.workspace.items))
		self._seed(self._create(ws_id, "Player movement", "agent"))
		self._create(ws_id, "Scratch", "ask")
		for name in ("chat.list", "chat.create", "chat.send", "chat.stop", "chat.history", "chat.approve"):
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
			turn.stop, turn.tokens_in, turn.tokens_out = "finished", 1200, len(answer) // 4
			chat.turns.append(turn)

	def snapshot(self, chat):
		recent = chat.turns[-SNAPSHOT_TURNS:]
		return dict(
			chat.summary(), model=MODEL, tasks=chat.tasks, hasMore=len(chat.turns) > len(recent),
			turns=[t.to_dict() for t in recent],
			pendingApprovals=[a.to_dict() for a in chat.approvals.values() if a.decision is None],
		)

	def get(self, chat_id):
		chat = self.items.get(chat_id)
		if chat is None:
			raise RpcError("not_found", "no chat %r" % chat_id)
		return chat

	def publish(self, chat, kind, data):
		self.server.hub.publish(chat.topic, kind, data)

	def on_restart(self):
		for chat in self.items.values():
			if chat.runner is not None and chat.state == "running":
				chat.stop_reason = "interrupted"
				chat.runner.cancel()

	async def m_chat_list(self, call, p):
		return {"chats": [c.summary() for c in self.items.values()]}

	async def m_chat_create(self, call, p):
		ws_id = self.server.workspace.get(p.str("wsId", next(iter(self.server.workspace.items)), required=False)).ws_id
		mode = p.str("mode", "agent", required=False)
		if mode not in ("ask", "plan", "agent"):
			raise RpcError("bad_request", "mode must be ask, plan or agent")
		chat = self._create(ws_id, p.str("title", "New chat", required=False), mode)
		self.server.hub.publish("ws:" + ws_id, "chat.created", chat.summary())
		return chat.summary()

	async def m_chat_history(self, call, p):
		chat = self.get(p.str("chatId"))
		before = p.str("before", None, required=False)
		limit = p.int("limit", 20, required=False)
		ids = [t.turn_id for t in chat.turns]
		end = ids.index(before) if before in ids else len(ids)
		start = max(0, end - limit)
		return {"turns": [t.to_dict() for t in chat.turns[start:end]], "hasMore": start > 0}

	async def m_chat_send(self, call, p):
		chat = self.get(p.str("chatId"))
		if chat.runner is not None:
			raise RpcError("busy", "chat %s is already running a turn" % chat.chat_id, {"state": chat.state})
		text = p.str("text")
		name = chat_scripts.pick(text, p.str("mockScript", None, required=False))
		script = chat_scripts.SCRIPTS.get(name)
		if script is None:
			raise RpcError("bad_request", "unknown mockScript %r (have: %s)" % (name, ", ".join(chat_scripts.SCRIPTS)))
		turn = Turn(chat.next_id("u"), text, chat.mode)
		chat.turns.append(turn)
		chat.state, chat.stop_reason = "running", "user_cancelled"
		chat.runner = asyncio.ensure_future(self._play(chat, turn, script))
		call.open_stream(lambda: self._stop(chat))
		return {"turnId": turn.turn_id, "script": name}

	async def m_chat_stop(self, call, p):
		chat = self.get(p.str("chatId"))
		self._stop(chat)
		return {"state": chat.state}

	def _stop(self, chat):
		if chat.runner is not None:
			chat.stop_reason = "user_cancelled"
			chat.runner.cancel()

	async def m_chat_approve(self, call, p):
		chat = self.get(p.str("chatId"))
		approval = chat.approvals.get(p.str("approvalId"))
		if approval is None:
			raise RpcError("not_found", "no approval %r" % p.str("approvalId"))
		decision = p.str("decision")
		if decision not in ("allow", "deny"):
			raise RpcError("bad_request", "decision must be allow or deny")
		if approval.decision is not None:
			raise RpcError("conflict", "already answered", {"decision": approval.decision, "by": approval.by})
		approval.decision, approval.by = decision, call.client_id
		approval.future.set_result(decision)
		self.publish(chat, "approval.resolved", {"approvalId": approval.approval_id, "decision": decision, "by": call.client_id})
		return {"decision": decision}

	async def _play(self, chat, turn, script):
		turn.tokens_in = 18000 + 40 * len(chat.turns)
		self.publish(chat, "turn.start", {"turnId": turn.turn_id, "mode": turn.mode, "group": "default", "user": {"text": turn.user_text}})
		try:
			for op in script:
				await self._op(chat, turn, op)
		except asyncio.CancelledError:
			self._close_open_segments(chat, turn)
			self._withdraw_approvals(chat)
			self._end(chat, turn, chat.stop_reason, {})
		finally:
			chat.runner = None
			chat.state = "idle"

	async def _op(self, chat, turn, op):
		kind = op[0]
		if kind in ("thinking", "text"):
			await self._stream_segment(chat, turn, Segment(chat.next_id("s"), kind, model=MODEL), op[1], "text")
		elif kind == "tool":
			await self._tool(chat, turn, op[1])
		elif kind == "tasks":
			chat.tasks = op[1]
			self.publish(chat, "tasks.changed", {"items": op[1]})
		elif kind == "pause":
			await self.server.clock.sleep(op[1])
		elif kind == "end":
			self._end(chat, turn, op[1], op[2])

	async def _stream_segment(self, chat, turn, segment, text, field):
		turn.segments.append(segment)
		start = dict(segment.extra, turnId=turn.turn_id, segId=segment.seg_id, type=segment.kind)
		self.publish(chat, "segment.start", start)
		for chunk in _chunks(text):
			await self.server.clock.sleep(DELTA_SECONDS)
			segment.text += chunk
			self.publish(chat, "segment.delta", {"segId": segment.seg_id, field: chunk})
		self._end_segment(chat, turn, segment)

	def _end_segment(self, chat, turn, segment):
		segment.done = True
		tokens = max(1, len(segment.text) // 4)
		turn.tokens_out += tokens
		self.publish(chat, "segment.end", {"segId": segment.seg_id, "tokens": tokens, "ms": int(tokens * DELTA_SECONDS * 1000)})

	def _close_open_segments(self, chat, turn):
		for segment in turn.segments:
			if not segment.done:
				self._end_segment(chat, turn, segment)

	async def _tool(self, chat, turn, spec):
		call_id = chat.next_id("k")
		segment = Segment(chat.next_id("s"), "tool", tool=spec["name"], callId=call_id)
		await self._stream_segment(chat, turn, segment, json.dumps(spec["args"]), "argsText")
		if spec.get("approval") and not await self._approve(chat, call_id, spec):
			self.publish(chat, "tool.state", {"callId": call_id, "state": "denied"})
			return
		self.publish(chat, "tool.state", {"callId": call_id, "state": "running"})
		await self.server.clock.sleep(spec.get("ms", 50) / 1000.0)
		self.publish(chat, "tool.state", {"callId": call_id, "state": "ok", "ms": spec.get("ms", 50), "result": spec["result"]})

	def _withdraw_approvals(self, chat):
		for approval in chat.approvals.values():
			if approval.decision is None:
				approval.decision = "withdrawn"
				self.publish(chat, "approval.resolved", {"approvalId": approval.approval_id, "decision": "withdrawn", "by": None})

	async def _approve(self, chat, call_id, spec):
		approval = Approval(chat.next_id("a"), call_id, spec["name"], spec["args"])
		chat.approvals[approval.approval_id] = approval
		chat.state = "awaiting_approval"
		self.publish(chat, "tool.state", {"callId": call_id, "state": "awaiting_approval"})
		self.publish(chat, "approval.request", dict(approval.to_dict(), summary="Run `%s`" % spec["args"].get("command", spec["name"])))
		decision = await approval.future
		chat.state = "running"
		return decision == "allow"

	def _end(self, chat, turn, stop, extra):
		turn.stop = stop
		turn.extra = dict(extra)
		data = dict(extra, turnId=turn.turn_id, stop=stop, tokensIn=turn.tokens_in, tokensOut=turn.tokens_out)
		self.publish(chat, "turn.end", data)


def _chunks(text):
	pieces = [m.group(0) for m in CHUNK.finditer(text) if m.group(0)]
	return ["".join(pieces[i:i + 2]) for i in range(0, len(pieces), 2)]
