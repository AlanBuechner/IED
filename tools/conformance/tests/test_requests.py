"""req/res, error codes, cancellation, opId idempotency (docs/10 'Requests: conventions')."""

import asyncio
import time

from harness import ProtocolTest
from iedclient import RpcFailure, new_id

ERROR_CODES = ["bad_request", "not_found", "forbidden", "untrusted", "conflict", "busy", "cancelled", "timeout", "unavailable", "internal", "plugin_error"]


class RequestTests(ProtocolTest):
	async def test_responses_carry_their_request_id_and_may_overtake(self):
		client = await self.client()
		slow_id, slow, _ = client.start_request("testhooks.delay", {"ms": 400, "result": "slow"})
		fast_id, fast, _ = client.start_request("testhooks.delay", {"ms": 0, "result": "fast"})
		done, _ = await asyncio.wait([slow, fast], return_when=asyncio.FIRST_COMPLETED, timeout=5)
		self.assertEqual({fast}, done)
		self.assertEqual(fast_id, fast.result()["id"])
		self.assertEqual("fast", fast.result()["result"]["result"])
		self.assertEqual("slow", (await asyncio.wait_for(slow, 5))["result"]["result"])
		self.assertEqual(slow_id, slow.result()["id"])

	async def test_requests_start_in_the_order_they_were_sent(self):
		"""Requests run side by side, but each socket's start in the order sent: keystrokes sent a request each (a
		terminal's, a browser page's) reach their target as typed."""
		client = await self.client()
		name = new_id("order")
		sent = [client.start_request("testhooks.counter", {"name": name}) for _ in range(200)]
		values = [(await asyncio.wait_for(future, 10))["result"]["value"] for _, future, _ in sent]
		self.assertEqual(list(range(1, 201)), values)

	async def test_every_error_code_passes_through_with_data(self):
		client = await self.client()
		for code in ERROR_CODES:
			res = await client.request_frame("testhooks.fail", {"code": code, "message": "m-" + code, "data": {"k": [1, 2]}})
			self.assertEqual({"code": code, "message": "m-" + code, "data": {"k": [1, 2]}}, res["error"])

	async def test_duplicate_in_flight_id_is_rejected(self):
		client = await self.client()
		first = asyncio.get_running_loop().create_future()
		client.pending[7] = first
		await client.send({"t": "req", "id": 7, "method": "testhooks.delay", "params": {"ms": 300}})
		await client.send({"t": "req", "id": 7, "method": "testhooks.delay", "params": {"ms": 0}})
		rejected = await asyncio.wait_for(first, 5)
		self.assertEqual("bad_request", rejected["error"]["code"])
		original = asyncio.get_running_loop().create_future()
		client.pending[7] = original
		self.assertTrue((await asyncio.wait_for(original, 5))["ok"])

	async def test_cancel_in_flight_request(self):
		client = await self.client()
		req_id, future, _ = client.start_request("testhooks.delay", {"ms": 10000})
		await asyncio.sleep(0.05)
		started = time.monotonic()
		await client.send({"t": "cancel", "id": req_id})
		res = await asyncio.wait_for(future, 5)
		self.assertEqual("cancelled", res["error"]["code"])
		self.assertLess(time.monotonic() - started, 2.0)

	async def test_cancel_of_finished_or_unknown_request_is_ignored(self):
		client = await self.client()
		await client.request("testhooks.delay", {"ms": 0})
		await client.send({"t": "cancel", "id": 1})
		await client.send({"t": "cancel", "id": 999999})
		await self.barrier(client)
		await asyncio.sleep(0.1)
		self.assertEqual({}, client.pending)

	async def test_cancel_stops_a_stream_the_request_opened(self):
		client = await self.client()
		topic = await self.new_topic()
		await client.sub([topic])
		req_id, future, _ = client.start_request("testhooks.stream", {"topic": topic, "count": 10000, "intervalMs": 5})
		self.assertTrue((await asyncio.wait_for(future, 5))["ok"])
		await client.wait_for(topic, lambda f: f.get("kind") == "tick" and f["data"]["i"] >= 5)
		await client.send({"t": "cancel", "id": req_id})
		frames = await client.wait_for(topic, lambda f: f.get("kind") == "stream.end")
		self.assertEqual("cancelled", frames[-1]["data"]["reason"])
		await asyncio.sleep(0.1)
		await self.barrier(client)
		self.assertEqual([], [f for f in client.drain(topic) if f.get("kind") == "tick"])

	async def test_op_id_returns_the_original_result(self):
		client = await self.client()
		name = new_id("n")
		op = new_id("op")
		first = await client.request("testhooks.counter", {"name": name}, op_id=op)
		again = await client.request("testhooks.counter", {"name": name}, op_id=op)
		self.assertEqual(first, again)
		fresh = await client.request("testhooks.counter", {"name": name})
		self.assertEqual(first["value"] + 1, fresh["value"], "the duplicate must not have executed")

	async def test_op_id_survives_reconnect_and_is_per_client(self):
		client = await self.client()
		name, op = new_id("n"), new_id("op")
		first = await client.request("testhooks.counter", {"name": name}, op_id=op)
		client.abort()
		again = await self.reconnect(client, resume=False)
		self.assertEqual(first, await again.request("testhooks.counter", {"name": name}, op_id=op))
		other = await self.client()
		self.assertEqual(first["value"] + 1, (await other.request("testhooks.counter", {"name": name}, op_id=op))["value"])

	async def test_op_id_retry_joins_a_request_still_in_flight(self):
		client = await self.client()
		op = new_id("op")
		client.start_request("testhooks.delay", {"ms": 500, "result": "once"}, op_id=op)
		await asyncio.sleep(0.1)
		client.abort()
		again = await self.reconnect(client, resume=False)
		retried = await again.request("testhooks.delay", {"ms": 500, "result": "once"}, op_id=op)
		later = await again.request("testhooks.delay", {"ms": 0})
		self.assertEqual("once", retried["result"])
		self.assertEqual(retried["execution"] + 1, later["execution"], "the retry must not start a second execution")

	async def test_op_id_replays_errors_too(self):
		client = await self.client()
		op = new_id("op")
		with self.assertRaises(RpcFailure) as first:
			await client.request("testhooks.fail", {"code": "busy"}, op_id=op)
		with self.assertRaises(RpcFailure) as again:
			await client.request("testhooks.fail", {"code": "busy"}, op_id=op)
		self.assertEqual(first.exception.code, again.exception.code)

	async def test_op_id_reused_for_another_method_is_rejected(self):
		client = await self.client()
		op = new_id("op")
		await client.request("testhooks.counter", {"name": new_id("n")}, op_id=op)
		with self.assertRaises(RpcFailure) as caught:
			await client.request("testhooks.delay", {"ms": 0}, op_id=op)
		self.assertEqual("bad_request", caught.exception.code)
