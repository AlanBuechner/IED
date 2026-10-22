"""The make-style dependency files clang writes (-dependency-file): `<target>: <source> <header> ...`, with lines
continued by a backslash and spaces in paths written as "\\ "."""


def parse(text):
	"""The files a dependency file lists after its target, in order."""
	_, separator, body = text.replace("\r\n", "\n").partition(".d:")
	if not separator:
		return []
	files, current, i = [], [], 0
	while i < len(body):
		char = body[i]
		following = body[i + 1] if i + 1 < len(body) else ""
		if char == "\\" and following == " ":
			current.append(" ")
			i += 2
			continue
		if char == "\\" and following == "\n":
			i += 2
			char = " "
		elif char == "$" and following == "$":
			current.append("$")
			i += 2
			continue
		else:
			i += 1
		if char in " \t\n":
			if current:
				files.append("".join(current))
				current = []
		else:
			current.append(char)
	if current:
		files.append("".join(current))
	return files
