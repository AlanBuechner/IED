"""A language server for the checks (docs/24): speaks LSP over stdio, takes UTF-8 positions when the client offers them,
publishes a diagnostic on every "bad" of an open document, answers hover with the position it got and definition with
another file, exits on a "fake/crash" request, and writes every message it gets to the JSON-lines file named by its
first argument."""

import json
import sys

LOG = open(sys.argv[1], "a", encoding="utf-8")
STDIN = sys.stdin.buffer
STDOUT = sys.stdout.buffer
documents = {}
encoding = "utf-16"


def read_message():
	length = None
	while True:
		line = STDIN.readline()
		if not line:
			return None
		line = line.strip()
		if not line:
			break
		name, _, value = line.decode("ascii").partition(":")
		if name.lower() == "content-length":
			length = int(value.strip())
	return json.loads(STDIN.read(length).decode("utf-8"))


def send(message):
	body = json.dumps(dict(message, jsonrpc="2.0")).encode("utf-8")
	STDOUT.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
	STDOUT.flush()


def column(line, index):
	"""The column of the character at `index` of `line`, in the negotiated encoding."""
	if encoding == "utf-8":
		return len(line[:index].encode("utf-8"))
	return len(line[:index].encode("utf-16-le")) // 2


def publish(uri):
	items = []
	for number, line in enumerate(documents[uri].split("\n")):
		at = line.find("bad")
		while at >= 0:
			start = {"line": number, "character": column(line, at)}
			end = {"line": number, "character": column(line, at + 3)}
			items.append({"range": {"start": start, "end": end}, "severity": 1, "source": "fake", "message": "bad word"})
			at = line.find("bad", at + 3)
	send({"method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": items}})


def answer(message):
	global encoding
	method, params = message["method"], message.get("params") or {}
	if method == "initialize":
		offered = params["capabilities"].get("general", {}).get("positionEncodings", [])
		encoding = "utf-8" if "utf-8" in offered else "utf-16"
		return {"capabilities": {"positionEncoding": encoding, "textDocumentSync": {"openClose": True, "change": 1, "save": True}, "hoverProvider": True, "definitionProvider": True}, "serverInfo": {"name": "fake"}}
	if method == "textDocument/hover":
		position = params["position"]
		end = dict(position, character=position["character"] + 3)
		return {"contents": "at %d:%d" % (position["line"], position["character"]), "range": {"start": position, "end": end}}
	if method == "textDocument/definition":
		uri = params["textDocument"]["uri"].rsplit("/", 1)[0] + "/b.fk"
		return [{"uri": uri, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}}}]
	if method == "fake/crash":
		sys.exit(3)
	return None


def notified(message):
	method, params = message["method"], message.get("params") or {}
	if method == "textDocument/didOpen":
		documents[params["textDocument"]["uri"]] = params["textDocument"]["text"]
		publish(params["textDocument"]["uri"])
	elif method == "textDocument/didChange":
		documents[params["textDocument"]["uri"]] = params["contentChanges"][-1]["text"]
		publish(params["textDocument"]["uri"])
	elif method == "exit":
		sys.exit(0)


def main():
	sys.stderr.write("fake-lsp started\n")
	sys.stderr.flush()
	while True:
		message = read_message()
		if message is None:
			return
		LOG.write(json.dumps(message) + "\n")
		LOG.flush()
		if "id" in message and "method" in message:
			send({"id": message["id"], "result": answer(message)})
		elif "method" in message:
			notified(message)


main()
