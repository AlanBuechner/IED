"""Console output. One Log is created per invocation and passed down; nothing prints behind its back."""

import sys


class Log:
	def __init__(self, stream=None, verbose=False, messages=None):
		self.stream = stream or sys.stdout
		self.verbose = verbose
		self.messages = messages      # where notes and warnings go when stdout carries --json output

	def json_mode(self):
		if self.messages is None:
			self.messages = sys.stderr

	def _message(self, text):
		target = self.messages or self.stream
		target.write(text + "\n")
		target.flush()

	def line(self, text=""):
		self.stream.write(text + "\n")
		self.stream.flush()

	def write(self, text):
		self.stream.write(text)
		self.stream.flush()

	def header(self, text):
		self.line(f"== {text} ==")

	def note(self, text):
		self._message(f"nuke: {text}")

	def warn(self, text):
		self._message(f"nuke: warning: {text}")

	def detail(self, text):
		if self.verbose:
			self._message(f"nuke: {text}")
