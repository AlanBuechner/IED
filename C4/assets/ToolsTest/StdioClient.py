# A client for the servers that speak framed JSON over stdio (C4LSP, C4DAP): starts the server, sends
# Content-Length frames and reads them back. Each scenario prints one line per failure and exits 1 when there
# was any.
#
# Usage:  python StdioClient.py lsp <C4LSP.exe>
#         python StdioClient.py dap <C4DAP.exe> <program.exe> <main.cf>
import json
import queue
import subprocess
import sys
import threading

REPLY_SECONDS = 20


class Server:
    def __init__(self, command: list[str]):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.messages: queue.Queue = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def send_raw(self, data: bytes):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def send(self, message: dict):
        body = json.dumps(message).encode("utf-8")
        self.send_raw(b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body)

    def next(self, seconds: float = REPLY_SECONDS) -> dict | None:
        try:
            return self.messages.get(timeout=seconds)
        except queue.Empty:
            return None

    def wait_for(self, matches, seconds: float = REPLY_SECONDS) -> dict | None:
        """The first message `matches` accepts; the ones before it are dropped."""
        while True:
            message = self.next(seconds)
            if message is None or matches(message):
                return message

    def exit_code(self, seconds: float = REPLY_SECONDS) -> int | None:
        try:
            return self.process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            self.process.kill()
            return None

    def _read(self):
        stream = self.process.stdout
        while True:
            length = None
            while True:
                line = stream.readline()
                if not line:
                    return
                line = line.strip()
                if not line:
                    break
                key, _, value = line.decode("ascii", "replace").partition(":")
                if key.strip().lower() == "content-length":
                    length = int(value.strip())
            if length is None:
                continue
            self.messages.put(json.loads(stream.read(length).decode("utf-8")))


def lsp_scenario(exe: str) -> list[str]:
    failures = []
    server = Server([exe])
    server.send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"processId": None, "rootUri": None, "capabilities": {}}})
    reply = server.wait_for(lambda m: m.get("id") == 1)
    if reply is None or "capabilities" not in reply.get("result", {}):
        failures.append(f"initialize got {reply}")
    server.send_raw(b"Content-Length: 9\r\n\r\n{not json")  # dropped; the next request still gets its answer (BUG-202)
    server.send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
    if server.wait_for(lambda m: m.get("id") == 2) is None:
        failures.append("no answer to shutdown after a malformed frame")
    server.send({"jsonrpc": "2.0", "method": "exit"})
    code = server.exit_code()
    if code != 0:
        failures.append(f"the server exited with {code} after shutdown and exit")
    return failures


class DapClient:
    def __init__(self, server: Server):
        self.server = server
        self.seq = 0
        self.events: list[dict] = []

    def request(self, command: str, arguments: dict | None = None) -> dict | None:
        self.seq += 1
        seq = self.seq
        self.server.send({"seq": seq, "type": "request", "command": command, "arguments": arguments or {}})
        return self.server.wait_for(lambda m: self._keep(m) and m.get("type") == "response" and m.get("request_seq") == seq)

    def event(self, name: str) -> dict | None:
        for index, seen in enumerate(self.events):
            if seen.get("event") == name:
                return self.events.pop(index)
        found = self.server.wait_for(lambda m: self._keep(m) and m.get("type") == "event" and m.get("event") == name)
        if found is not None:
            self.events.remove(found)
        return found

    def _keep(self, message: dict) -> bool:
        if message.get("type") == "event":
            self.events.append(message)
        return True


# A session of `program` (main.cf beside it, built with -g): `Twice` on line 3 stops at its breakpoint with the
# right stack and locals, then the program runs to its end with its output and exit code.
def dap_scenario(exe: str, program: str, source: str) -> list[str]:
    failures = []
    client = DapClient(Server([exe]))
    if not (client.request("initialize", {"adapterID": "c4"}) or {}).get("success"):
        return ["initialize failed"]
    launch = client.request("launch", {"program": program})
    if not (launch or {}).get("success"):
        return [f"launch failed: {launch}"]
    bound = client.request("setBreakpoints", {"source": {"path": source}, "breakpoints": [{"line": 3}]})
    if not bound or not bound["body"]["breakpoints"][0]["verified"]:
        failures.append(f"the breakpoint did not bind: {bound}")
    client.request("configurationDone")
    stop = client.event("stopped")
    if stop is None or stop["body"]["reason"] != "breakpoint":
        return failures + [f"no stop at the breakpoint: {stop}"]
    thread = stop["body"]["threadId"]
    frames = client.request("stackTrace", {"threadId": thread})["body"]["stackFrames"]
    if frames[0]["name"] != "Twice" or frames[0]["line"] != 3 or frames[1]["name"] != "main":
        failures.append(f"the stack is {[(f['name'], f['line']) for f in frames[:2]]}")
    scopes = client.request("scopes", {"frameId": frames[0]["id"]})["body"]["scopes"]
    variables = client.request("variables", {"variablesReference": scopes[0]["variablesReference"]})["body"]["variables"]
    if not any(v["name"] == "value" and v["value"] == "3" for v in variables):
        failures.append(f"the locals are {variables}")
    client.request("continue", {"threadId": thread})
    exited = client.event("exited")
    if exited is None or exited["body"]["exitCode"] != 0:
        failures.append(f"the program ended with {exited}")
    output = "".join(e["body"]["output"] for e in client.events if e.get("event") == "output")
    if "hello from the debuggee" not in output:
        failures.append(f"the program's output was {output!r}")
    client.request("disconnect")
    code = client.server.exit_code()
    if code != 0:
        failures.append(f"the adapter exited with {code} after disconnect")
    return failures


SCENARIOS = {"lsp": lsp_scenario, "dap": dap_scenario}

if __name__ == "__main__":
    found = SCENARIOS[sys.argv[1]](*sys.argv[2:])
    for failure in found:
        print(failure)
    sys.exit(1 if found else 0)
