"""A fake Claude Code CLI for IED's checks (docs/04 "The Claude Code provider"): the parts of Claude Code 2.1.304's
print mode IED uses - `--version`, `auth status`, and `-p --input-format stream-json --output-format stream-json`
with control requests both ways (initialize, get_usage, get_context_usage, set_model, interrupt; IED's in-process MCP server reached over
mcp_message: initialize, tools/list, tools/call), stream_event partial messages, rate_limit_events and results.

    python fake_claude.py --state <folder> [the CLI's own arguments]

The state folder scripts it and records what it saw (README.md): script.json (the responses, taken in order),
usage.json (get_usage's answer), context.json (get_context_usage's), auth.json (`auth status`), log.jsonl (what it was started with and every line it
read), sessions/ (the session ids --resume finds)."""

import json
import os
import queue
import sys
import threading
import time
import uuid

VERSION = "2.1.304"
# get_context_usage's answer unless context.json says another: the rows the CLI's /context shows, each with its kind.
CONTEXT_USAGE = {"totalTokens": 41234, "maxTokens": 1000000, "rawMaxTokens": 1000000, "percentage": 4, "model": "claude-opus-5-5", "categories": [
	{"name": "System prompt", "tokens": 6100, "color": "promptBorder", "kind": "used"}, {"name": "MCP tools", "tokens": 17900, "color": "cyan_FOR_SUBAGENTS_ONLY", "kind": "used"},
	{"name": "Messages", "tokens": 17234, "color": "purple_FOR_SUBAGENTS_ONLY", "kind": "used"}, {"name": "Free space", "tokens": 913766, "color": "promptBorder", "kind": "free"},
	{"name": "Autocompact buffer", "tokens": 45000, "color": "inactive", "kind": "buffer"}]}


class State:
	def __init__(self, folder):
		self.folder = folder
		os.makedirs(os.path.join(folder, "sessions"), exist_ok=True)
		self.lock = threading.Lock()

	def path(self, name):
		return os.path.join(self.folder, name)

	def read(self, name, default):
		try:
			with open(self.path(name), encoding="utf-8") as f:
				return json.load(f)
		except (OSError, ValueError):
			return default

	def log(self, entry):
		with self.lock, open(self.path("log.jsonl"), "a", encoding="utf-8") as f:
			f.write(json.dumps(entry) + "\n")

	def next_response(self):
		"""The next scripted response, taken off script.json; None when it has none left."""
		with self.lock:
			script = self.read("script.json", {"responses": []})
			if not script.get("responses"):
				return None
			response = script["responses"].pop(0)
			with open(self.path("script.json"), "w", encoding="utf-8") as f:
				json.dump(script, f)
			return response


def out(message):
	sys.stdout.write(json.dumps(message) + "\n")
	sys.stdout.flush()


class Cli:
	def __init__(self, state, args):
		self.state = state
		self.args = args
		self.model = self.flag("--model") or "default"
		self.session = self.flag("--resume") or str(uuid.uuid4())
		self.lines = queue.Queue()
		self.backlog = []
		self.tools = []
		self.next_id = 0
		self.interrupted = False
		self.started = False

	def flag(self, name):
		return self.args[self.args.index(name) + 1] if name in self.args and self.args.index(name) + 1 < len(self.args) else None

	# --- Lines in ---------------------------------------------------------------------------------------------------

	def read_stdin(self):
		for raw in sys.stdin.buffer:
			text = raw.decode("utf-8", "replace").strip()
			if not text:
				continue
			try:
				message = json.loads(text)
			except ValueError:
				continue
			self.state.log({"in": message})
			self.lines.put(message)
		self.lines.put(None)

	def take(self, timeout=None):
		if self.backlog:
			return self.backlog.pop(0)
		try:
			return self.lines.get(timeout=timeout)
		except queue.Empty:
			return "timeout"

	def ask(self, request):
		"""A control request of the CLI's to IED; its answer's response. Other lines wait in the backlog, but an
		interrupt is taken at once."""
		self.next_id += 1
		request_id = "cli-%d" % self.next_id
		out({"type": "control_request", "request_id": request_id, "request": request})
		held = []
		while True:
			message = self.lines.get()
			if message is None:
				sys.exit(0)
			if message.get("type") == "control_response" and message["response"].get("request_id") == request_id:
				self.backlog = held + self.backlog
				return message["response"].get("response", {})
			if message.get("type") == "control_request" and message["request"].get("subtype") == "interrupt":
				self.answer(message, {"still_queued": []})
				self.interrupted = True
				continue
			held.append(message)

	def mcp(self, message):
		return self.ask({"subtype": "mcp_message", "server_name": "ied", "message": message}).get("mcp_response", {})

	def answer(self, message, response):
		out({"type": "control_response", "response": {"subtype": "success", "request_id": message["request_id"], "response": response}})

	# --- IED's MCP server --------------------------------------------------------------------------------------------

	def connect_ied(self):
		self.mcp({"method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "claude-code", "version": VERSION}}, "jsonrpc": "2.0", "id": 0})
		self.mcp({"jsonrpc": "2.0", "method": "notifications/initialized"})
		self.list_tools()

	def list_tools(self):
		self.next_id += 1
		reply = self.mcp({"method": "tools/list", "jsonrpc": "2.0", "id": self.next_id})
		self.tools = [tool["name"] for tool in reply.get("result", {}).get("tools", [])]
		self.state.log({"tools": self.tools})

	# --- Control requests from IED -----------------------------------------------------------------------------------

	def control(self, message):
		request = message["request"]
		subtype = request.get("subtype")
		if subtype == "initialize":
			if "ied" in request.get("sdkMcpServers", []):
				self.connect_ied()
			self.answer(message, {"commands": [], "models": [{"value": "opus", "resolvedModel": "claude-opus-5-5"}, {"value": "sonnet", "resolvedModel": "claude-sonnet-5-5"}, {"value": "haiku", "resolvedModel": "claude-haiku-5-5"}]})
		elif subtype == "get_usage":
			self.answer(message, self.state.read("usage.json", {"session": {}, "subscription_type": None, "rate_limits_available": False, "rate_limits": None, "behaviors": None}))
		elif subtype == "get_context_usage":
			self.answer(message, self.state.read("context.json", CONTEXT_USAGE))
		elif subtype == "set_model":
			self.model = request.get("model") or "default"
			self.answer(message, {})
		elif subtype == "interrupt":
			self.interrupted = True
			self.answer(message, {"still_queued": []})
		elif subtype == "mcp_message":
			self.answer(message, {})
			if request.get("message", {}).get("method") == "notifications/tools/list_changed":
				self.list_tools()
		else:
			out({"type": "control_response", "response": {"subtype": "error", "request_id": message["request_id"], "error": "Unsupported control request subtype: %s" % subtype}})

	# --- A query -----------------------------------------------------------------------------------------------------

	def resolved(self):
		return {"opus": "claude-opus-5-5", "sonnet": "claude-sonnet-5-5", "haiku": "claude-haiku-5-5", "fable": "claude-fable-5-1", "default": "claude-opus-5-5"}.get(self.model, self.model)

	def event(self, event):
		out({"type": "stream_event", "event": event, "session_id": self.session, "parent_tool_use_id": None, "uuid": str(uuid.uuid4())})

	def result(self, text, is_error=False, subtype="success"):
		out({"type": "result", "subtype": subtype, "is_error": is_error, "result": text, "session_id": self.session, "num_turns": 1,
			"modelUsage": {self.resolved(): {"inputTokens": 10, "outputTokens": 5, "contextWindow": 1000000, "maxOutputTokens": 128000}}})

	def respond(self, response):
		"""One model response as stream events; the tool calls it makes, or None when it ends the query."""
		if response.get("rateLimit"):
			out({"type": "rate_limit_event", "rate_limit_info": response["rateLimit"], "uuid": str(uuid.uuid4()), "session_id": self.session})
		if response.get("error"):
			out({"type": "assistant", "message": {"id": str(uuid.uuid4()), "model": "<synthetic>", "role": "assistant", "stop_reason": "stop_sequence", "type": "message", "content": [{"type": "text", "text": response.get("text", "")}]},
				"parent_tool_use_id": None, "session_id": self.session, "uuid": str(uuid.uuid4()), "error": response["error"], "is_api_error_message": True})
			self.result(response.get("text", ""), is_error=True)
			return None
		blocks = response.get("blocks", [])
		calls = []
		delay = response.get("delayMs", 0) / 1000.0
		self.event({"type": "message_start", "message": {"id": "msg_" + uuid.uuid4().hex[:12], "model": self.resolved(), "usage": {"input_tokens": 12, "cache_read_input_tokens": 3000, "cache_creation_input_tokens": 100, "output_tokens": 1}}})
		for index, block in enumerate(blocks):
			if self.interrupted:
				break
			if "thinking" in block:
				self.event({"type": "content_block_start", "index": index, "content_block": {"type": "thinking", "thinking": ""}})
				self.event({"type": "content_block_delta", "index": index, "delta": {"type": "thinking_delta", "thinking": block["thinking"]}})
			elif "text" in block:
				self.event({"type": "content_block_start", "index": index, "content_block": {"type": "text", "text": ""}})
				for word in block["text"].split(" "):
					self.event({"type": "content_block_delta", "index": index, "delta": {"type": "text_delta", "text": word + " "}})
					time.sleep(delay)
					self.poll()
					if self.interrupted:
						break
			else:
				call_id = block.get("id") or "toolu_" + uuid.uuid4().hex[:12]
				self.event({"type": "content_block_start", "index": index, "content_block": {"type": "tool_use", "id": call_id, "name": "mcp__ied__" + block["tool"], "input": {}}})
				self.event({"type": "content_block_delta", "index": index, "delta": {"type": "input_json_delta", "partial_json": json.dumps(block.get("input", {}))}})
				calls.append((call_id, block["tool"], block.get("input", {})))
			self.event({"type": "content_block_stop", "index": index})
		if self.interrupted:
			self.result("[Request interrupted by user]", is_error=True, subtype="error_during_execution")
			return None
		stop = response.get("stop") or ("tool_use" if calls else "end_turn")
		self.event({"type": "message_delta", "delta": {"stop_reason": stop}, "usage": {"output_tokens": 42}})
		self.event({"type": "message_stop"})
		if stop != "tool_use":
			self.result(" ".join(b.get("text", "") for b in blocks).strip())
			return None
		return calls

	def poll(self):
		"""An interrupt that came while a response streams."""
		try:
			while True:
				message = self.lines.get_nowait()
				if message is None:
					self.lines.put(None)
					return
				if message.get("type") == "control_request" and message["request"].get("subtype") == "interrupt":
					self.answer(message, {"still_queued": []})
					self.interrupted = True
				else:
					self.backlog.append(message)
		except queue.Empty:
			return

	def query(self, message):
		if not self.started:
			self.started = True
			with open(os.path.join(self.state.folder, "sessions", self.session), "w", encoding="utf-8") as f:
				f.write(self.model)
			out({"type": "system", "subtype": "init", "session_id": self.session, "model": self.resolved(), "tools": ["mcp__ied__" + t for t in self.tools], "claude_code_version": VERSION,
				"apiKeySource": "ANTHROPIC_API_KEY" if os.environ.get("ANTHROPIC_API_KEY") else self.state.read("auth.json", {}).get("apiKeySource", "none")})
		self.state.log({"query": message.get("message", {}).get("content")})
		self.interrupted = False
		while True:
			response = self.state.next_response() or {"blocks": [{"text": "(the fake has no response queued)"}]}
			calls = self.respond(response)
			if calls is None:
				return
			for call_id, tool, arguments in calls:
				self.next_id += 1
				reply = self.mcp({"method": "tools/call", "params": {"name": tool, "arguments": arguments, "_meta": {"claudecode/toolUseId": call_id}}, "jsonrpc": "2.0", "id": self.next_id})
				self.state.log({"toolResult": {"id": call_id, "tool": tool, "result": reply.get("result")}})
				if self.interrupted:
					break
			if self.interrupted:
				self.result("[Request interrupted by user]", is_error=True, subtype="error_during_execution")
				return

	def run(self):
		resume = self.flag("--resume")
		if resume and not os.path.exists(os.path.join(self.state.folder, "sessions", resume)):
			print("No conversation found with session ID: " + resume, flush=True)
			self.result("No conversation found with session ID: " + resume, is_error=True, subtype="error_during_execution")
			return 1
		threading.Thread(target=self.read_stdin, daemon=True).start()
		while True:
			message = self.take()
			if message is None:
				return 0
			if message == "timeout":
				continue
			kind = message.get("type")
			if kind == "control_request":
				self.control(message)
			elif kind == "user":
				self.query(message)


def main(argv):
	state = State(argv[argv.index("--state") + 1] if "--state" in argv else os.path.join(os.getcwd(), "fake-claude-state"))
	args = [a for i, a in enumerate(argv) if a != "--state" and (i == 0 or argv[i - 1] != "--state")]
	# the names of the variables that would bill an API key or another provider - never their values
	billed = sorted(k for k in os.environ if k.startswith("ANTHROPIC_") or k in ("CLAUDE_CODE_USE_BEDROCK", "CLAUDE_CODE_USE_VERTEX", "CLAUDE_CODE_USE_FOUNDRY"))
	# a parent Claude Code session's variables (bare mode, its ids) that would make this CLI ignore the owner's login
	inherited = sorted(k for k in os.environ if k in ("CLAUDECODE", "CLAUDE_CODE_SIMPLE", "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_SESSION_ID", "CLAUDE_CODE_OAUTH_SCOPES"))
	state.log({"argv": args, "cwd": os.getcwd(), "env": {k: v for k, v in os.environ.items() if k.startswith("CLAUDE_CODE_DISABLE_")}, "billed": billed, "inherited": inherited})
	if args[:1] == ["--version"]:
		print(VERSION + " (Claude Code)")
		return 0
	if args[:2] == ["auth", "status"]:
		if os.environ.get("ANTHROPIC_API_KEY"):
			auth = {"loggedIn": True, "authMethod": "api_key", "apiProvider": "firstParty"}
		elif os.environ.get("CLAUDE_CODE_SIMPLE"):
			auth = {"loggedIn": False, "authMethod": "none", "apiProvider": "firstParty"}   # bare mode never reads the login
		else:
			auth = state.read("auth.json", {"loggedIn": True, "authMethod": "claude.ai", "apiProvider": "firstParty"})
		print(json.dumps({k: v for k, v in auth.items() if k != "apiKeySource"}))
		return 0
	if args[:2] == ["auth", "login"]:
		# the person finishes the login in the browser: the CLI is signed in from now on
		method = "console" if "--console" in args else "claude.ai"
		with open(state.path("auth.json"), "w", encoding="utf-8") as f:
			json.dump({"loggedIn": True, "authMethod": method, "apiProvider": "firstParty"}, f)
		print("Login successful.")
		return 0
	if "-p" not in args:
		print("the fake only knows print mode", file=sys.stderr)
		return 2
	return Cli(state, args).run()


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
