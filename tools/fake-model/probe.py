"""Probe: sends a chat completion to an OpenAI-compatible server and records exactly what the wire did.

	python -I tools/fake-model/probe.py --script stall-prefill            # in-process fake server, prints the timeline
	python -I tools/fake-model/probe.py --all --time-scale 0.02          # runs every script and checks its 'wire' block
	python -I tools/fake-model/probe.py --url http://127.0.0.1:8080 --model qwen   # probe a real server

Tool-call deltas are merged by id when present, else by index (docs/12) - a reference for the IED harness.
"""

import argparse
import http.client
import json
import pathlib
import sys
import threading
import time
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import scripting  # noqa: E402

DEFAULT_MESSAGES = [{"role": "system", "content": "You are a test."}, {"role": "user", "content": "Read src/main.c"}]


class Observation:
	def __init__(self):
		self.status = None
		self.timeline = []
		self.transport = None
		self.text = ""
		self.reasoning = ""
		self.deltas = []
		self.finish_reason = None
		self.usage = None
		self.malformed = 0
		self.comments = 0
		self.error = ""
		self.max_silence = 0.0

	def tool_calls(self):
		return merge_tool_calls(self.deltas)

	def summary(self):
		calls = ", ".join("%s(%s)" % (c["name"], "valid" if _valid_json(c["arguments"]) else "INVALID") for c in self.tool_calls())
		return "transport=%s status=%s finish=%r text=%r tools=[%s] malformed=%d comments=%d maxSilence=%.2fs%s" % (
			self.transport, self.status, self.finish_reason, _clip(self.text), calls, self.malformed, self.comments,
			self.max_silence, " error=%r" % _clip(self.error) if self.error else "")


def merge_tool_calls(deltas):
	calls = []
	for delta in deltas:
		index = delta.get("index", 0)
		if delta.get("id"):
			call = next((c for c in calls if c["id"] == delta["id"]), None)
		else:
			call = next((c for c in reversed(calls) if c["index"] == index), None)
		if call is None:
			call = {"id": delta.get("id"), "index": index, "name": "", "arguments": ""}
			calls.append(call)
		function = delta.get("function") or {}
		if function.get("name") and not call["name"]:
			call["name"] = function["name"]
		call["arguments"] += function.get("arguments") or ""
	return calls


def _valid_json(text):
	try:
		json.loads(text)
		return True
	except ValueError:
		return False


def _clip(text, n=60):
	return text if len(text) <= n else text[:n] + "..."


def probe(base_url, script=None, stream=True, time_scale=None, model=None, messages=None, timeout=300):
	parts = urllib.parse.urlsplit(base_url)
	headers = {"Content-Type": "application/json"}
	if script:
		headers["X-Fake-Script"] = script
	if time_scale is not None:
		headers["X-Fake-Time-Scale"] = str(time_scale)
	body = {"model": model or "fake-model", "messages": messages or DEFAULT_MESSAGES, "stream": stream, "stream_options": {"include_usage": True}}
	obs = Observation()
	conn = http.client.HTTPConnection(parts.hostname, parts.port or 80, timeout=timeout)
	try:
		_exchange(conn, (parts.path.rstrip("/") or "") + "/v1/chat/completions", body, headers, obs)
	finally:
		conn.close()
	return obs


def _exchange(conn, path, body, headers, obs):
	clock = Clock(obs)
	try:
		conn.request("POST", path, json.dumps(body), headers)
		response = conn.getresponse()
	except (http.client.RemoteDisconnected, ConnectionError) as e:
		clock.mark("no response (%s)" % type(e).__name__)
		obs.transport = "no_response"
		return
	obs.status = response.status
	clock.mark("headers %d" % response.status)
	if response.status >= 400:
		obs.error = response.read().decode("utf-8", "replace")
		obs.transport = "http_error"
		clock.mark("error body")
		return
	(_read_stream if body["stream"] else _read_whole)(response, obs, clock)


class Clock:
	def __init__(self, obs):
		self.obs = obs
		self.start = self.last = time.monotonic()

	def mark(self, what):
		now = time.monotonic()
		self.obs.max_silence = max(self.obs.max_silence, now - self.last)
		self.last = now
		self.obs.timeline.append((now - self.start, what))


def _read_stream(response, obs, clock):
	saw_done = False
	try:
		while True:
			line = response.readline()
			if not line:
				break
			line = line.decode("utf-8", "replace").rstrip("\r\n")
			if not line:
				continue
			clock.mark(describe(line))
			if line.startswith(":"):
				obs.comments += 1
			elif line.startswith("data:"):
				saw_done = _take_data(obs, line[5:].strip()) or saw_done
	except (http.client.IncompleteRead, ConnectionError, OSError) as e:
		clock.mark("RESET (%s)" % type(e).__name__)
		obs.transport = "reset"
		return
	clock.mark("EOF")
	obs.transport = "done" if saw_done else "eof"


def describe(line):
	"""One short human-readable label for an SSE line, for the probe timeline."""
	if not line.startswith("data:"):
		return line[:100]
	payload = line[5:].strip()
	if payload == "[DONE]":
		return "[DONE]"
	try:
		chunk = json.loads(payload)
	except ValueError:
		return "MALFORMED %r" % payload[:60]
	parts = ["error %s" % json.dumps(chunk["error"])[:80]] if "error" in chunk else []
	if chunk.get("usage"):
		parts.append("usage %s" % json.dumps({k: v for k, v in chunk["usage"].items() if k != "timings"}))
	for choice in chunk.get("choices", []):
		delta = choice.get("delta") or {}
		parts += ["%s=%r" % (k, v) for k, v in delta.items() if k != "tool_calls" and v not in ("", None)]
		parts += [_describe_call(call) for call in delta.get("tool_calls") or []]
		if choice.get("finish_reason") is not None:
			parts.append("finish_reason=%s" % choice["finish_reason"])
	return " ".join(parts) or "data (empty delta)"


def _describe_call(call):
	function = call.get("function") or {}
	bits = ["index=%s" % call.get("index")]
	if call.get("id"):
		bits.append("id=%s" % call["id"])
	if function.get("name"):
		bits.append("name=%s" % function["name"])
	bits.append("args=%r" % function.get("arguments", ""))
	return "tool_call(%s)" % " ".join(bits)


def _take_data(obs, payload):
	if payload == "[DONE]":
		return True
	try:
		chunk = json.loads(payload)
	except ValueError:
		obs.malformed += 1
		return False
	if "error" in chunk:
		obs.error += json.dumps(chunk["error"])
	if chunk.get("usage"):
		obs.usage = chunk["usage"]
	for choice in chunk.get("choices", []):
		delta = choice.get("delta") or {}
		obs.text += delta.get("content") or ""
		obs.reasoning += "".join(delta.get(k) or "" for k in ("reasoning_content", "reasoning", "thinking"))
		obs.deltas.extend(delta.get("tool_calls") or [])
		if choice.get("finish_reason") is not None:
			obs.finish_reason = choice["finish_reason"]
	return False


def _read_whole(response, obs, clock):
	try:
		raw = response.read()
	except (http.client.IncompleteRead, ConnectionError, OSError) as e:
		clock.mark("RESET (%s)" % type(e).__name__)
		obs.transport = "reset"
		return
	clock.mark("body %d bytes" % len(raw))
	try:
		body = json.loads(raw)
	except ValueError:
		obs.malformed += 1
		obs.transport = "done"
		return
	message = body["choices"][0]["message"]
	obs.text = message.get("content") or ""
	obs.reasoning = "".join(message.get(k) or "" for k in ("reasoning_content", "reasoning", "thinking"))
	obs.deltas = [dict(c, index=i) for i, c in enumerate(message.get("tool_calls") or [])]
	obs.finish_reason = body["choices"][0].get("finish_reason")
	obs.usage = body.get("usage")
	obs.transport = "done"


def check(obs, wire, time_scale):
	"""Compares an observation with a script response's 'wire' block; returns a list of mismatches."""
	problems = []

	def expect(label, want, got):
		if want != got:
			problems.append("%s: expected %r, got %r" % (label, want, got))

	expect("transport", wire.get("transport", "done"), obs.transport)
	if "status" in wire:
		expect("status", wire["status"], obs.status)
	if "finishReason" in wire:
		expect("finishReason", wire["finishReason"], obs.finish_reason)
	if "text" in wire:
		expect("text", wire["text"], obs.text)
	for key, haystack in (("textIncludes", obs.text), ("reasoningIncludes", obs.reasoning), ("errorIncludes", obs.error)):
		if key in wire and wire[key] not in haystack:
			problems.append("%s: %r not found in %r" % (key, wire[key], _clip(haystack, 200)))
	if "toolCalls" in wire:
		got = [{"name": c["name"], "argumentsValid": _valid_json(c["arguments"])} for c in obs.tool_calls()]
		expect("toolCalls", wire["toolCalls"], got)
	if "malformed" in wire:
		expect("malformed", wire["malformed"], obs.malformed)
	if "comments" in wire and obs.comments < wire["comments"]:
		problems.append("comments: expected at least %d, got %d" % (wire["comments"], obs.comments))
	if "minSilence" in wire and obs.max_silence < wire["minSilence"] * time_scale * 0.7:
		problems.append("minSilence: longest silence %.3fs < %.3fs" % (obs.max_silence, wire["minSilence"] * time_scale))
	if "maxSilence" in wire and obs.max_silence > wire["maxSilence"] * time_scale + 0.25:
		problems.append("maxSilence: longest silence %.3fs > %.3fs" % (obs.max_silence, wire["maxSilence"] * time_scale))
	return problems


def run_all(base_url, library, time_scale, out):
	"""Resets the server's per-script counters, runs every response of every script in order; returns failures."""
	reset(base_url)
	failures = 0
	for name, script in sorted(library.scripts.items()):
		for index, response in enumerate(script["responses"]):
			obs = probe(base_url, name, stream=True, time_scale=time_scale)
			problems = check(obs, response.get("wire", {}), time_scale)
			out.write("%-4s %-30s #%d %-24s %s\n" % ("FAIL" if problems else "ok", name, index, script["stopClass"], obs.summary()))
			for problem in problems:
				out.write("       %s\n" % problem)
			failures += bool(problems)
	return failures


def reset(base_url):
	parts = urllib.parse.urlsplit(base_url)
	conn = http.client.HTTPConnection(parts.hostname, parts.port or 80, timeout=10)
	conn.request("POST", "/_fake/reset", "{}", {"Content-Type": "application/json"})
	conn.getresponse().read()
	conn.close()


def start_local_server(time_scale):
	import fake_model
	args = fake_model.parse_args(["--port", "0", "--time-scale", str(time_scale)])
	server = fake_model.make_server(args)
	threading.Thread(target=server.serve_forever, daemon=True).start()
	return server, "http://127.0.0.1:%d" % server.server_address[1]


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--url", help="server base URL (default: start the fake server in-process)")
	parser.add_argument("--script", help="script name (sent as X-Fake-Script)")
	parser.add_argument("--all", action="store_true", help="run and check every script")
	parser.add_argument("--time-scale", type=float, default=None, help="X-Fake-Time-Scale for the request(s)")
	parser.add_argument("--no-stream", action="store_true")
	parser.add_argument("--model")
	args = parser.parse_args(argv)
	server, url = (None, args.url) if args.url else start_local_server(1.0)
	if args.all:
		scale = 0.02 if args.time_scale is None else args.time_scale
		failures = run_all(url, scripting.Library(HERE / "scripts"), scale, sys.stdout)
		sys.stdout.write("%d failure(s)\n" % failures)
		return 1 if failures else 0
	obs = probe(url, args.script, not args.no_stream, args.time_scale, args.model)
	for at, what in obs.timeline:
		sys.stdout.write("+%8.3fs  %s\n" % (at, what))
	sys.stdout.write(obs.summary() + "\n")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
