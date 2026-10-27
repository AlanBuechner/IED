"""Fake language tools for the checks and the e2e scenarios (docs/26 "Language tools"): programs that are batch files
answering `--version` (and, for a compiler, `install <name>@v<version>`, writing that tool into %GOBIN% as `go install`
does), zip archives holding one, and a loopback HTTP server that serves them with their .sha256 files - so detection and
installs run end to end and nothing is downloaded from anywhere."""

import hashlib
import http.server
import io
import os
import threading
import zipfile


def version_script(name, version, extra=""):
	"""A batch file printing "<name> version <version>" for --version; `extra` runs before the version check."""
	return "\r\n".join(["@echo off", extra, 'if "%~1"=="--version" (', "  echo %s version %s" % (name, version), "  exit /b 0", ")",
		'if "%~1"=="install" goto install', "echo %s: %%*" % name, "exit /b 0",
		":install",
		'for /f "tokens=1,2 delims=@" %%a in ("%~2") do (',
		"  set NAME=%%a", "  set VER=%%b", ")",
		"set VER=%VER:v=%",
		'> "%GOBIN%\\%NAME%.cmd" echo @echo off',
		'>> "%GOBIN%\\%NAME%.cmd" echo echo %NAME% version %VER%',
		"echo installed %NAME% %VER% into %GOBIN%",
		"exit /b 0", ""])


def broken_script(message):
	"""A batch file that fails as a rustup proxy without its component does."""
	return "@echo off\r\necho %s\r\nexit /b 1\r\n" % message


def write_program(folder, name, text):
	os.makedirs(folder, exist_ok=True)
	path = os.path.join(folder, name + ".cmd")
	with open(path, "w", encoding="ascii", newline="") as f:
		f.write(text)
	return path


def tool_zip(name, version, folder="bin", top="fake-%s"):
	"""A zip of one program under <top>/<folder>/ (top stripped by `strip: 1`): its bytes and SHA-256."""
	buffer = io.BytesIO()
	with zipfile.ZipFile(buffer, "w") as archive:
		archive.writestr("%s/%s/%s.cmd" % (top % version, folder, name), version_script(name, version))
		archive.writestr("%s/README.txt" % (top % version), "a fake %s %s\r\n" % (name, version))
	data = buffer.getvalue()
	return data, hashlib.sha256(data).hexdigest()


class FileServer:
	"""Serves `files` ({path: bytes}) on 127.0.0.1 until stopped; `requests` lists the paths asked for."""

	def __init__(self, files):
		self.files = dict(files)
		self.requests = []
		owner = self

		class Handler(http.server.BaseHTTPRequestHandler):
			def do_GET(self):
				owner.requests.append(self.path)
				data = owner.files.get(self.path)
				if data is None:
					self.send_response(404)
					self.end_headers()
					return
				self.send_response(200)
				self.send_header("Content-Length", str(len(data)))
				self.end_headers()
				self.wfile.write(data)

			def log_message(self, *args):
				pass

		self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
		self.port = self.httpd.server_address[1]
		self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
		self.thread.start()

	def url(self, path):
		return "http://127.0.0.1:%d%s" % (self.port, path)

	def stop(self):
		self.httpd.shutdown()
		self.httpd.server_close()
