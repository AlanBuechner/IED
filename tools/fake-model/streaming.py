"""Executes one scripted response: SSE over chunked HTTP (stream=true) or one JSON body (stream=false)."""

import json
import socket
import struct
import time

import dialects

UNSET = object()
DEFAULT_MALFORMED = '{"choices": [{"index": 0, "delta": {"content": "half a chun'


class Aborted(Exception):
	pass


class Ended(Exception):
	"""The response is complete (an HTTP error body was sent); later steps are not run."""


class StreamSink:
	def __init__(self, handler, lazy_headers):
		self.handler = handler
		self.lazy = lazy_headers
		self.headers_sent = False
		self.bytes_sent = 0

	def start(self):
		if not self.lazy:
			self.send_headers()

	def send_headers(self):
		if self.headers_sent:
			return
		h = self.handler
		h.send_response(200)
		h.send_header("Content-Type", "text/event-stream")
		h.send_header("Cache-Control", "no-cache")
		h.send_header("Transfer-Encoding", "chunked")
		h.end_headers()
		h.wfile.flush()
		self.headers_sent = True

	def write(self, text):
		self.send_headers()
		data = text.encode("utf-8")
		try:
			self.handler.wfile.write(b"%x\r\n%s\r\n" % (len(data), data))
			self.handler.wfile.flush()
		except (ConnectionError, OSError):
			raise Aborted("client went away")
		self.bytes_sent += len(data)

	def data(self, obj):
		self.write("data: %s\n\n" % (obj if isinstance(obj, str) else json.dumps(obj)))

	def comment(self, text):
		self.write(": %s\n\n" % text)

	def finish(self):
		self.send_headers()
		try:
			self.handler.wfile.write(b"0\r\n\r\n")
			self.handler.wfile.flush()
		except (ConnectionError, OSError):
			pass

	def abort(self):
		abort_connection(self.handler)

	def error(self, status, body, headers):
		if self.headers_sent:
			self.data({"error": body.get("error", body) if isinstance(body, dict) else body})
			return
		send_json(self.handler, status, body, headers)
		raise Ended()


class CollectSink:
	"""stream=false: gathers the script's output into one chat.completion body."""

	def __init__(self, handler):
		self.handler = handler
		self.headers_sent = False
		self.content = ""
		self.reasoning = {}
		self.calls = {}
		self.finish_reason = None
		self.usage = None
		self.malformed = None

	def start(self):
		pass

	def send_headers(self):
		pass

	def comment(self, text):
		pass

	def write(self, text):
		pass

	def data(self, obj):
		if obj == "[DONE]":
			return
		if isinstance(obj, str):
			self.malformed = obj
			return
		if obj.get("usage"):
			self.usage = obj["usage"]
		for choice in obj.get("choices", []):
			self._take(choice)

	def _take(self, choice):
		delta = choice.get("delta", {})
		self.content += delta.get("content") or ""
		for field in ("reasoning_content", "reasoning", "thinking"):
			if delta.get(field):
				self.reasoning[field] = self.reasoning.get(field, "") + delta[field]
		for call in delta.get("tool_calls", []):
			slot = self.calls.setdefault(call.get("index", 0), {"id": None, "type": "function", "function": {"name": "", "arguments": ""}})
			slot["id"] = slot["id"] or call.get("id")
			slot["function"]["name"] = slot["function"]["name"] or call.get("function", {}).get("name", "")
			slot["function"]["arguments"] += call.get("function", {}).get("arguments", "")
		if choice.get("finish_reason") is not None:
			self.finish_reason = choice["finish_reason"]

	def finish(self):
		if self.malformed is not None:
			send_raw(self.handler, 200, self.malformed.encode("utf-8"), "application/json")
			return
		message = {"role": "assistant", "content": self.content or None}
		message.update(self.reasoning)
		if self.calls:
			message["tool_calls"] = [self.calls[i] for i in sorted(self.calls)]
		body = {"object": "chat.completion", "choices": [{"index": 0, "message": message, "finish_reason": self.finish_reason}]}
		if self.usage:
			body["usage"] = self.usage
		send_json(self.handler, 200, body, {})

	def abort(self):
		abort_connection(self.handler)

	def error(self, status, body, headers):
		send_json(self.handler, status, body, headers)
		raise Ended()


def send_json(handler, status, body, headers):
	send_raw(handler, status, json.dumps(body).encode("utf-8"), "application/json", headers)


def send_raw(handler, status, data, content_type, headers=None):
	try:
		handler.send_response(status)
		handler.send_header("Content-Type", content_type)
		handler.send_header("Content-Length", str(len(data)))
		for key, value in (headers or {}).items():
			handler.send_header(key, str(value))
		handler.end_headers()
		handler.wfile.write(data)
		handler.wfile.flush()
	except (ConnectionError, OSError):
		pass


def abort_connection(handler):
	"""Resets the TCP connection (RST, no FIN): what a crashed or killed model server looks like."""
	handler.close_connection = True
	sock = handler.connection
	try:
		sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
		sock.close()
	except OSError:
		pass
	raise Aborted("cut by script")


class Runner:
	def __init__(self, sink, ctx, response):
		self.sink = sink
		self.ctx = ctx
		self.delay = response.get("chunkDelayMs", 15) / 1000.0
		self.native_calls = 0
		self.emitted = 0

	def run(self, steps):
		first = next((k for k in steps[0] if k in OPS), None) if steps else None
		if first not in ("error", "cut"):
			self.sink.start()
		try:
			for step in steps:
				op = next(k for k in step if k in OPS)
				getattr(self, OPS[op])(step)
		except Ended:
			return
		self.sink.finish()

	def sleep(self, seconds):
		if seconds > 0:
			time.sleep(seconds * self.ctx["time_scale"])

	def chunk(self, delta=None, finish=None, extra=None):
		obj = {
			"id": self.ctx["completion_id"], "object": "chat.completion.chunk", "created": self.ctx["created"],
			"model": self.ctx["model"], "system_fingerprint": "fake-model",
			"choices": [{"index": 0, "delta": delta if delta is not None else {}, "logprobs": None, "finish_reason": finish}],
		}
		obj.update(extra or {})
		return obj

	def _pieces(self, text, size):
		return [text[i:i + size] for i in range(0, len(text), size)] or [""]

	def _stream_field(self, step, field, text):
		delay = step.get("delayMs", self.delay * 1000) / 1000.0
		for piece in self._pieces(text, step.get("chunk", 6)):
			self.sleep(delay)
			self.sink.data(self.chunk({field: piece}))
			self.emitted += len(piece)

	def op_headers(self, step):
		self.sink.send_headers()

	def op_role(self, step):
		self.sink.data(self.chunk({"role": "assistant", "content": ""}))

	def op_text(self, step):
		self._stream_field(step, "content", step["text"])

	def op_reasoning(self, step):
		self._stream_field(step, step.get("field", "reasoning_content"), step["reasoning"])

	def op_text_tool_call(self, step):
		spec = step["textToolCall"]
		self._stream_field(step, "content", dialects.render(spec.get("dialect", "hermes"), spec["name"], spec["arguments"]))

	def op_tool_call(self, step):
		spec = step["toolCall"]
		index = spec.get("index", self.native_calls)
		self.native_calls += 1
		call_id = spec.get("id", "call_%s_%d" % (self.ctx["request"], self.native_calls))
		id_on, name_on = spec.get("idOn", "first"), spec.get("nameOn", "first")
		arguments = spec["arguments"] if isinstance(spec["arguments"], str) else json.dumps(spec["arguments"])
		head = {"index": index, "type": "function", "function": {"name": spec["name"], "arguments": ""}}
		if id_on != "none":
			head["id"] = call_id
		self.sink.data(self.chunk({"tool_calls": [head]}))
		delay = step.get("delayMs", self.delay * 1000) / 1000.0
		for piece in self._pieces(arguments, spec.get("argChunk", 12)):
			self.sleep(delay)
			part = {"index": index, "function": {"arguments": piece}}
			if id_on == "every":
				part["id"] = call_id
			if name_on == "every":
				part["function"]["name"] = spec["name"]
			self.sink.data(self.chunk({"tool_calls": [part]}))
			self.emitted += len(piece)

	def op_stall(self, step):
		remaining = float(step["stall"])
		every = step.get("keepalive")
		while remaining > 0:
			slice_ = min(remaining, every) if every else remaining
			self.sleep(slice_)
			remaining -= slice_
			if every and remaining > 0:
				self.sink.comment("keepalive")

	def op_cut(self, step):
		if step["cut"] == "before-headers" and self.sink.headers_sent:
			raise ValueError("cut 'before-headers' after headers were sent")
		self.sink.abort()

	def op_malformed(self, step):
		raw = step["malformed"]
		self.sink.data(raw if isinstance(raw, str) else DEFAULT_MALFORMED)

	def op_raw(self, step):
		self.sink.write(step["raw"])

	def op_finish(self, step):
		self.sink.data(self.chunk({}, step["finish"]))

	def op_usage(self, step):
		usage = step["usage"] if isinstance(step["usage"], dict) else self._usage()
		self.sink.data(self.chunk(extra={"choices": [], "usage": usage}))

	def _usage(self):
		completion = max(1, self.emitted // 4)
		prompt = self.ctx["prompt_tokens"]
		return {
			"prompt_tokens": prompt, "completion_tokens": completion, "total_tokens": prompt + completion,
			"timings": {"prompt_n": prompt, "prompt_per_second": 850.0, "predicted_n": completion, "predicted_per_second": 42.0},
		}

	def op_done(self, step):
		self.sink.data("[DONE]")

	def op_error(self, step):
		error = step["error"]
		self.sink.error(error["status"], error.get("body", {"error": {"message": "scripted error"}}), error.get("headers", {}))


OPS = {
	"headers": "op_headers", "role": "op_role", "text": "op_text", "reasoning": "op_reasoning",
	"toolCall": "op_tool_call", "textToolCall": "op_text_tool_call", "stall": "op_stall", "cut": "op_cut",
	"malformed": "op_malformed", "raw": "op_raw", "finish": "op_finish", "usage": "op_usage", "done": "op_done",
	"error": "op_error",
}
