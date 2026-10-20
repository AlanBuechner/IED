import http.client
import io
import json
import pathlib
import sys
import threading
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))

import probe  # noqa: E402
import scripting  # noqa: E402

SCALE = 0.02


class FakeModelTests(unittest.TestCase):
	def setUp(self):
		self.server, self.url = probe.start_local_server(1.0)
		self.port = self.server.server_address[1]

	def tearDown(self):
		self.server.shutdown()
		self.server.server_close()

	def call(self, method, path, body=None, headers=None):
		conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
		conn.request(method, path, json.dumps(body) if body is not None else None, dict({"Content-Type": "application/json"}, **(headers or {})))
		response = conn.getresponse()
		data = response.read()
		conn.close()
		return response.status, json.loads(data) if data else None

	def test_every_script_matches_its_wire_block(self):
		out = io.StringIO()
		failures = probe.run_all(self.url, scripting.Library(HERE / "scripts"), SCALE, out)
		self.assertEqual(0, failures, out.getvalue())

	def test_every_docs12_stop_class_has_a_script(self):
		classes = {s["stopClass"] for s in scripting.Library(HERE / "scripts").scripts.values()}
		required = {
			"connect_failed", "http_error", "disconnected_mid_stream", "stalled_prefill", "stalled_stream", "max_tokens",
			"empty", "reasoning_only", "announced_no_action", "unparsed_tool_call", "malformed_tool_call", "unknown_finish",
			"context_overflow", "loop_detected", "no_progress", "budget", "user_cancelled",
		}
		self.assertEqual(set(), required - classes)

	def test_readme_documents_every_script(self):
		readme = (HERE / "README.md").read_text(encoding="utf-8")
		missing = [name for name in scripting.Library(HERE / "scripts").scripts if "`%s`" % name not in readme]
		self.assertEqual([], missing)

	def test_non_streaming_requests_get_one_completion_body(self):
		obs = probe.probe(self.url, "ok-tool-call", stream=False, time_scale=0)
		self.assertEqual(("done", "tool_calls", ["read_file"]), (obs.transport, obs.finish_reason, [c["name"] for c in obs.tool_calls()]))
		obs = probe.probe(self.url, "text-tool-hermes", stream=False, time_scale=0)
		self.assertIn("<tool_call>", obs.text)

	def test_discovery_endpoints_follow_config(self):
		self.assertEqual(32768, self.call("GET", "/props")[1]["default_generation_settings"]["n_ctx"])
		self.call("POST", "/_fake/config", {"nCtx": 65536, "slots": 4})
		props = self.call("GET", "/props")[1]
		self.assertEqual((16384, 4), (props["default_generation_settings"]["n_ctx"], props["total_slots"]))
		self.assertEqual(16384, self.call("GET", "/v1/models")[1]["data"][0]["max_model_len"])
		self.assertEqual(4, len(self.call("GET", "/slots")[1]))
		self.assertEqual("num_ctx 65536", self.call("POST", "/api/show", {"model": "fake-model"})[1]["parameters"])
		self.assertEqual(400, self.call("POST", "/_fake/config", {"slots": "four"})[0])

	def test_enforced_context_rejects_large_prompts_in_both_styles(self):
		self.call("POST", "/_fake/config", {"nCtx": 256, "enforceCtx": True})
		big = [{"role": "user", "content": "x" * 4000}]
		status, body = self.call("POST", "/v1/chat/completions", {"messages": big})
		self.assertEqual((400, 256, "exceed_context_size_error"), (status, body["error"]["n_ctx"], body["error"]["type"]))
		self.call("POST", "/_fake/config", {"ctxErrorStyle": "openai"})
		status, body = self.call("POST", "/v1/chat/completions", {"messages": big})
		self.assertIn("maximum context length is 256 tokens", body["error"]["message"])
		self.assertEqual(200, self.call("POST", "/v1/chat/completions", {"messages": [{"role": "user", "content": "hi"}]})[0])

	def test_queue_model_prefix_and_default_selection(self):
		inline = {"description": "d", "stopClass": "none", "harness": "h", "responses": [{"steps": [{"text": "inline!"}, {"finish": "stop"}]}]}
		self.assertEqual(200, self.call("POST", "/_fake/queue", {"scripts": ["empty", inline]})[0])
		self.assertEqual("", probe.probe(self.url, time_scale=0).text)
		self.assertEqual("inline!", probe.probe(self.url, time_scale=0).text)
		self.assertEqual("Hello from the fake model.", probe.probe(self.url, time_scale=0).text)
		self.assertEqual("length", probe.probe(self.url, model="script:max-tokens", time_scale=0).finish_reason)
		self.assertEqual(400, self.call("POST", "/_fake/queue", {"scripts": [{"responses": []}]})[0])
		self.assertEqual(400, self.call("POST", "/v1/chat/completions", {"messages": []}, {"X-Fake-Script": "no-such-script"})[0])

	def test_request_log_and_placeholders(self):
		probe.probe(self.url, "budget-runaway", time_scale=0)
		second = probe.probe(self.url, "budget-runaway", time_scale=0)
		self.assertEqual({"path": "src/file_2.c"}, json.loads(second.tool_calls()[0]["arguments"]))
		error = json.loads(probe.probe(self.url, "context-overflow-llamacpp", time_scale=0).error)
		self.assertEqual(32768, error["error"]["n_ctx"])
		log = self.call("GET", "/_fake/requests")[1]
		self.assertEqual(["budget-runaway", "budget-runaway", "context-overflow-llamacpp"], [e["script"] for e in log])
		self.assertGreater(log[0]["promptTokens"], 0)

	def test_slots_bound_concurrency(self):
		done = {}

		def run(name, script):
			probe.probe(self.url, script, time_scale=0.01)
			done[name] = time.monotonic()

		started = time.monotonic()
		slow = threading.Thread(target=run, args=("slow", "stall-prefill-after-headers"))
		slow.start()
		time.sleep(0.2)
		self.assertTrue(self.call("GET", "/slots")[1][0]["is_processing"])
		run("fast", "ok-text")
		slow.join()
		self.assertGreaterEqual(done["fast"], done["slow"] - 0.05, "with one slot the second request waits for the first")
		self.assertGreater(done["fast"] - started, 1.0)

	def test_script_validation_rejects_unknown_ops(self):
		bad = {"description": "d", "stopClass": "x", "harness": "h", "responses": [{"steps": [{"explode": True}]}]}
		with self.assertRaises(scripting.ScriptError):
			scripting.validate(bad, "bad")


if __name__ == "__main__":
	unittest.main()
