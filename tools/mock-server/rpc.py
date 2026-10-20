"""Request dispatch: method registry, typed params, stable error codes, opId idempotency (docs/10)."""

import asyncio
import time
import traceback

ERROR_CODES = (
	"bad_request", "not_found", "forbidden", "untrusted", "conflict", "busy",
	"cancelled", "timeout", "unavailable", "internal", "plugin_error",
)
OP_TTL_SECONDS = 600


class RpcError(Exception):
	def __init__(self, code, message, data=None):
		if code not in ERROR_CODES:
			raise ValueError("unknown error code %r" % code)
		super().__init__(message)
		self.code = code
		self.message = message
		self.data = data

	def to_error(self):
		error = {"code": self.code, "message": self.message}
		if self.data is not None:
			error["data"] = self.data
		return error


class Params:
	def __init__(self, raw):
		self.raw = raw if isinstance(raw, dict) else {}

	def _get(self, name, kinds, label, default, required):
		if name not in self.raw:
			if required:
				raise RpcError("bad_request", "missing param %r" % name)
			return default
		value = self.raw[name]
		if not isinstance(value, kinds) or (bool not in _as_tuple(kinds) and isinstance(value, bool)):
			raise RpcError("bad_request", "param %r must be %s" % (name, label))
		return value

	def str(self, name, default=None, required=True):
		return self._get(name, str, "a string", default, required)

	def int(self, name, default=None, required=True):
		return self._get(name, int, "an integer", default, required)

	def num(self, name, default=None, required=True):
		return self._get(name, (int, float), "a number", default, required)

	def bool(self, name, default=None, required=True):
		return self._get(name, bool, "a boolean", default, required)

	def list(self, name, default=None, required=True):
		return self._get(name, list, "an array", default, required)

	def obj(self, name, default=None, required=True):
		return self._get(name, dict, "an object", default, required)

	def any(self, name, default=None):
		return self.raw.get(name, default)


def _as_tuple(kinds):
	return kinds if isinstance(kinds, tuple) else (kinds,)


class Call:
	"""What a handler sees: the calling session, the request id and opId, and a way to register a stream."""

	def __init__(self, session, req_id, op_id):
		self.session = session
		self.req_id = req_id
		self.op_id = op_id

	@property
	def client_id(self):
		return self.session.client_id

	def open_stream(self, cancel_fn):
		self.session.streams[self.req_id] = cancel_fn


class OpCache:
	def __init__(self):
		self.entries = {}

	def lookup(self, client_id, op_id, method):
		self._expire()
		entry = self.entries.get((client_id, op_id))
		if entry is None:
			return None
		if entry[1] != method:
			raise RpcError("bad_request", "opId %r was already used for %s" % (op_id, entry[1]))
		return entry[2]

	def store(self, client_id, op_id, method, task):
		self.entries[(client_id, op_id)] = (time.monotonic(), method, task)

	def clear(self):
		self.entries.clear()

	def _expire(self):
		cutoff = time.monotonic() - OP_TTL_SECONDS
		for key in [k for k, v in self.entries.items() if v[0] < cutoff]:
			del self.entries[key]


class Rpc:
	def __init__(self):
		self.methods = {}
		self.ops = OpCache()

	def register(self, name, handler):
		if name in self.methods:
			raise KeyError("method registered twice: %s" % name)
		self.methods[name] = handler

	async def call(self, session, frame):
		"""Runs one request; returns ('ok', result) or ('err', error dict)."""
		method = frame["method"]
		handler = self.methods.get(method)
		if handler is None:
			return "err", RpcError("not_found", "unknown method %r" % method, {"method": method}).to_error()
		call = Call(session, frame["id"], frame.get("opId"))
		params = Params(frame.get("params", {}))
		if call.op_id is None:
			return await _outcome(handler(call, params))
		try:
			task = self.ops.lookup(session.client_id, call.op_id, method)
		except RpcError as e:
			return "err", e.to_error()
		if task is None:
			task = asyncio.ensure_future(_outcome(handler(call, params)))
			self.ops.store(session.client_id, call.op_id, method, task)
		try:
			return await asyncio.shield(task)
		except asyncio.CancelledError:
			task.cancel()
			raise


async def _outcome(awaitable):
	try:
		return "ok", await awaitable
	except RpcError as e:
		return "err", e.to_error()
	except asyncio.CancelledError:
		return "err", RpcError("cancelled", "request cancelled").to_error()
	except Exception as e:  # a handler bug must surface as 'internal', not kill the session
		traceback.print_exc()
		return "err", RpcError("internal", "%s: %s" % (type(e).__name__, e)).to_error()
