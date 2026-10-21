"""Scriptable OpenAI-compatible model server for harness tests (docs/12 stop taxonomy, docs/19). Stdlib only.

	python -I tools/fake-model/fake_model.py [--port 8089] [--n-ctx 32768] [--slots 1] [--time-scale 1.0]

Pick a script per request with the X-Fake-Script header, "model": "script:<name>", the /_fake/queue FIFO,
or --default-script. Prints 'LISTENING http://<host>:<port>' once ready. See tools/fake-model/README.md.
"""

import argparse
import collections
import json
import pathlib
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import scripting  # noqa: E402
import streaming  # noqa: E402

SCRIPTS_DIR = HERE / "scripts"
LOG_LIMIT = 500


class Config:
	def __init__(self, n_ctx=32768, n_ctx_train=131072, slots=1, model="fake-model", time_scale=1.0, enforce_ctx=False, ctx_error_style="llamacpp", default_script="ok-text", vision=False):
		self.n_ctx = n_ctx
		self.n_ctx_train = n_ctx_train
		self.slots = slots
		self.model = model
		self.time_scale = time_scale
		self.enforce_ctx = enforce_ctx
		self.ctx_error_style = ctx_error_style
		self.default_script = default_script
		self.vision = vision

	@property
	def slot_ctx(self):
		return self.n_ctx // max(1, self.slots)

	def to_dict(self):
		return {
			"nCtx": self.n_ctx, "nCtxTrain": self.n_ctx_train, "slots": self.slots, "slotCtx": self.slot_ctx, "model": self.model,
			"timeScale": self.time_scale, "enforceCtx": self.enforce_ctx, "ctxErrorStyle": self.ctx_error_style, "defaultScript": self.default_script,
			"vision": self.vision,
		}


CONFIG_FIELDS = {"nCtx": ("n_ctx", int), "nCtxTrain": ("n_ctx_train", int), "slots": ("slots", int), "model": ("model", str),
	"timeScale": ("time_scale", (int, float)), "enforceCtx": ("enforce_ctx", bool), "ctxErrorStyle": ("ctx_error_style", str), "defaultScript": ("default_script", str),
	"vision": ("vision", bool)}


class State:
	"""Everything the request threads share, behind one lock."""

	def __init__(self, config, library):
		self.config = config
		self.library = library
		self.lock = threading.Lock()
		self.counters = collections.Counter()
		self.queue = collections.deque()
		self.log = collections.deque(maxlen=LOG_LIMIT)
		self.requests = 0
		self.busy = 0
		self.slot_gate = threading.Semaphore(config.slots)

	def select(self, headers, body):
		"""Returns (name, script, response index, request number) for one completion request."""
		with self.lock:
			self.requests += 1
			name, script = self._choose(headers, body)
			index = min(self.counters[name], len(script["responses"]) - 1)
			self.counters[name] += 1
			return name, script, index, self.requests

	def _choose(self, headers, body):
		named = headers.get("X-Fake-Script") or _script_from_model(body.get("model", ""))
		if named:
			return named, self.library.get(named)
		if self.queue:
			item = self.queue.popleft()
			if isinstance(item, dict):
				return "inline-%d" % self.requests, scripting.validate(item, "inline script")
			return item, self.library.get(item)
		return self.config.default_script, self.library.get(self.config.default_script)

	def configure(self, changes):
		with self.lock:
			for key, value in changes.items():
				if key not in CONFIG_FIELDS or not isinstance(value, CONFIG_FIELDS[key][1]) or isinstance(value, bool) != (CONFIG_FIELDS[key][1] is bool):
					raise ValueError("bad config field %r" % key)
				setattr(self.config, CONFIG_FIELDS[key][0], value)
			if "slots" in changes:
				self.slot_gate = threading.Semaphore(self.config.slots)
			return self.config.to_dict()

	def reset(self):
		with self.lock:
			self.counters.clear()
			self.queue.clear()
			self.log.clear()


def _script_from_model(model):
	for prefix in ("script:", "fake/"):
		if model.startswith(prefix):
			return model[len(prefix):]
	return None


def estimate_prompt_tokens(body):
	chars = 0
	for message in body.get("messages", []):
		content = message.get("content")
		if isinstance(content, list):
			content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
		chars += len(content or "") + 16
	chars += len(json.dumps(body.get("tools", [])))
	return max(1, chars // 4)


def context_error(config, prompt_tokens):
	limit = config.slot_ctx
	if config.ctx_error_style == "openai":
		message = "This model's maximum context length is %d tokens. However, your messages resulted in %d tokens. Please reduce the length of the messages." % (limit, prompt_tokens)
		return {"error": {"message": message, "type": "invalid_request_error", "param": "messages", "code": "context_length_exceeded"}}
	message = "the request exceeds the available context size, try increasing it"
	return {"error": {"code": 400, "message": message, "type": "exceed_context_size_error", "n_prompt_tokens": prompt_tokens, "n_ctx": limit}}


class Handler(BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"
	server_version = "fake-model/1"

	@property
	def state(self):
		return self.server.state

	def log_message(self, fmt, *args):
		if self.server.verbose:
			sys.stderr.write("[fake-model] %s\n" % (fmt % args))

	def do_GET(self):
		path = urllib.parse.urlsplit(self.path).path
		config = self.state.config
		routes = {
			"/health": lambda: {"status": "ok"},
			"/props": self._props,
			"/slots": lambda: [{"id": i, "n_ctx": config.slot_ctx, "is_processing": i < self.state.busy} for i in range(config.slots)],
			"/v1/models": self._models,
			"/_fake/scripts": lambda: {n: {"stopClass": s["stopClass"], "description": s["description"]} for n, s in sorted(self.state.library.scripts.items())},
			"/_fake/requests": lambda: list(self.state.log),
			"/_fake/config": config.to_dict,
		}
		if path not in routes:
			return self._json(404, {"error": {"message": "no route %s" % path}})
		return self._json(200, routes[path]())

	def do_POST(self):
		path = urllib.parse.urlsplit(self.path).path
		try:
			body = self._read_json()
		except ValueError as e:
			return self._json(400, {"error": {"message": "invalid JSON body: %s" % e, "type": "invalid_request_error"}})
		if path in ("/v1/chat/completions", "/chat/completions"):
			return self._completion(body)
		if path == "/api/show":
			return self._json(200, self._ollama_show())
		if path == "/_fake/queue":
			return self._enqueue(body)
		if path == "/_fake/reset":
			self.state.reset()
			return self._json(200, {"ok": True})
		if path == "/_fake/config":
			try:
				return self._json(200, self.state.configure(body))
			except ValueError as e:
				return self._json(400, {"error": {"message": str(e)}})
		return self._json(404, {"error": {"message": "no route %s" % path}})

	def _props(self):
		config = self.state.config
		return {
			"default_generation_settings": {"n_ctx": config.slot_ctx, "params": {"temperature": 0.8, "n_predict": -1}},
			"total_slots": config.slots, "model_path": "models/%s.gguf" % config.model,
			"chat_template": "{{ messages }}", "build_info": "fake-model", "modalities": {"vision": config.vision, "audio": False},
		}

	def _models(self):
		config = self.state.config
		return {"object": "list", "data": [{
			"id": config.model, "object": "model", "created": 1700000000, "owned_by": "fake-model",
			"max_model_len": config.slot_ctx, "meta": {"n_ctx_train": config.n_ctx_train},
		}]}

	def _ollama_show(self):
		config = self.state.config
		return {"model_info": {"general.architecture": "fake", "fake.context_length": config.n_ctx_train}, "parameters": "num_ctx %d" % config.n_ctx}

	def _enqueue(self, body):
		items = body.get("scripts")
		if not isinstance(items, list):
			return self._json(400, {"error": {"message": "expected {\"scripts\": [name | script, ...]}"}})
		try:
			for item in items:
				scripting.validate(item, "queued script") if isinstance(item, dict) else self.state.library.get(item)
		except (KeyError, ValueError) as e:
			return self._json(400, {"error": {"message": str(e)}})
		with self.state.lock:
			self.state.queue.extend(items)
			return self._json(200, {"queued": len(self.state.queue)})

	def _completion(self, body):
		if not isinstance(body.get("messages"), list):
			return self._json(400, {"error": {"message": "'messages' is required", "type": "invalid_request_error"}})
		try:
			name, script, index, number = self.state.select(self.headers, body)
		except (KeyError, ValueError) as e:
			return self._json(400, {"error": {"message": str(e), "type": "invalid_request_error"}})
		config = self.state.config
		prompt_tokens = estimate_prompt_tokens(body)
		entry = {"request": number, "script": name, "response": index, "promptTokens": prompt_tokens, "stream": bool(body.get("stream")), "at": time.time(), "body": body}
		self.state.log.append(entry)
		if config.enforce_ctx and prompt_tokens > config.slot_ctx:
			entry["outcome"] = "context_error"
			return self._json(400, context_error(config, prompt_tokens))
		response = scripting.substitute(script["responses"][index], {"n_ctx": config.slot_ctx, "prompt_tokens": prompt_tokens, "request": number, "model": config.model})
		entry["outcome"] = self._run(body, response, number, prompt_tokens)

	def _run(self, body, response, number, prompt_tokens):
		scale = float(self.headers.get("X-Fake-Time-Scale", self.state.config.time_scale))
		ctx = {"completion_id": "chatcmpl-fake-%d" % number, "created": int(time.time()), "model": body.get("model") or self.state.config.model,
			"time_scale": scale, "request": number, "prompt_tokens": prompt_tokens, "return_progress": body.get("return_progress") is True}
		sink = streaming.StreamSink(self, response.get("headers") == "lazy") if body.get("stream") else streaming.CollectSink(self)
		with self.state.slot_gate:
			with self.state.lock:
				self.state.busy += 1
			try:
				streaming.Runner(sink, ctx, response).run(response.get("steps", []))
				return "completed"
			except streaming.Aborted as e:
				self.close_connection = True
				return "aborted: %s" % e
			finally:
				with self.state.lock:
					self.state.busy -= 1

	def _read_json(self):
		length = int(self.headers.get("Content-Length") or 0)
		raw = self.rfile.read(length) if length else b"{}"
		body = json.loads(raw.decode("utf-8"))
		if not isinstance(body, dict):
			raise ValueError("body must be a JSON object")
		return body

	def _json(self, status, obj):
		streaming.send_json(self, status, obj, {})


class FakeModelServer(ThreadingHTTPServer):
	daemon_threads = True

	def __init__(self, address, state, verbose=False):
		super().__init__(address, Handler)
		self.state = state
		self.verbose = verbose


def parse_args(argv):
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--host", default="127.0.0.1")
	parser.add_argument("--port", type=int, default=8089)
	parser.add_argument("--n-ctx", type=int, default=32768, help="total context; each slot gets n_ctx / slots (llama.cpp -np)")
	parser.add_argument("--n-ctx-train", type=int, default=131072)
	parser.add_argument("--slots", type=int, default=1)
	parser.add_argument("--model", default="fake-model")
	parser.add_argument("--time-scale", type=float, default=1.0, help="multiplies every stall and chunk delay")
	parser.add_argument("--enforce-ctx", action="store_true", help="reject prompts larger than the slot context")
	parser.add_argument("--ctx-error-style", choices=("llamacpp", "openai"), default="llamacpp")
	parser.add_argument("--default-script", default="ok-text")
	parser.add_argument("--scripts", default=str(SCRIPTS_DIR))
	parser.add_argument("--list", action="store_true", help="print the scripts and exit")
	parser.add_argument("--verbose", action="store_true")
	return parser.parse_args(argv)


def make_server(args):
	library = scripting.Library(args.scripts)
	config = Config(args.n_ctx, args.n_ctx_train, args.slots, args.model, args.time_scale, args.enforce_ctx, args.ctx_error_style, args.default_script)
	library.get(config.default_script)
	return FakeModelServer((args.host, args.port), State(config, library), args.verbose)


def main(argv):
	args = parse_args(argv)
	if args.list:
		for name, script in sorted(scripting.Library(args.scripts).scripts.items()):
			sys.stdout.write("%-32s %-26s %s\n" % (name, script["stopClass"], script["description"]))
		return 0
	server = make_server(args)
	sys.stdout.write("LISTENING http://%s:%d\n" % server.server_address[:2])
	sys.stdout.flush()
	try:
		server.serve_forever()
	except KeyboardInterrupt:
		pass
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
