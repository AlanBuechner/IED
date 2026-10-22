"""A compact API digest of a C4 library's headers as markdown: one section per type (its public members as declared,
bodies cut, with the first sentence of their doc comments; extension methods under the type they extend) and one per
header's free functions, so a docs search for a type or function name lands on its exact signatures. A variant asks
for one in its variant.json (`apiDigests`), made from the run's own toolchain.

	vendor\\python\\python.exe -I tools/ai-evals/lang/api_digest.py <library folder> <out.md> [title]
"""
import os
import re
import sys

KEYWORDS = ("return", "if", "while", "for", "switch", "case", "else", "do", "include", "delete", "new", "break", "continue")
TYPE_DECL = re.compile(r"^(template\s*<[^{]*?>\s*)?(class|struct|union)\s+(\w+)")
ENUM_DECL = re.compile(r"^enum\s+(class\s+)?(\w+)")
NAMESPACE = re.compile(r"^namespace\s+([\w:]+)")
ATTRS = re.compile(r"^\[([^\]]*)\]\s*")
OPERATOR = re.compile(r'operator\("([^"]+)"\)')
FUNCTION = re.compile(r"^(template\s*<[^{]*?>\s*)?(static\s+|inline\s+|extern\s+|const\s+)*[\w:<>,\*&\[\]\s]+?[\s\*&](~?\w+)\s*\(")


def strip_strings(line):
	"""The line with string and char literal contents blanked, so their braces and slashes do not count."""
	out = []
	quote = None
	i = 0
	while i < len(line):
		c = line[i]
		if quote:
			if c == "\\":
				out.append("  ")
				i += 2
				continue
			if c == quote:
				quote = None
				out.append(c)
			else:
				out.append(" ")
		elif c in "\"'":
			quote = c
			out.append(c)
		elif line.startswith("//", i):
			break
		else:
			out.append(c)
		i += 1
	return "".join(out)


def comment_of(line):
	code = strip_strings(line)
	at = len(code)
	return line[at + 2:].strip() if line[at:at + 2] == "//" else ""


def first_sentence(text, most=110):
	text = " ".join(text.split())
	m = re.search(r"\.(\s|$)", text)
	if m and m.start() > 15:
		text = text[:m.start() + 1]
	return text if len(text) <= most else text[:most - 3].rstrip() + "..."


def cut_body(code):
	"""A declaration without its body: up to the first '{' outside parentheses after the parameter list, or ';'."""
	depth = 0
	seen_paren = False
	for i, c in enumerate(code):
		if c == "(":
			depth += 1
			seen_paren = True
		elif c == ")":
			depth -= 1
		elif depth == 0 and c == "{" and seen_paren:
			return code[:i].rstrip()
		elif depth == 0 and c == ";":
			return code[:i].rstrip()
		elif depth == 0 and c == "=" and seen_paren and code[i:i + 2] != "==":
			return code[:i].rstrip()
	return code.rstrip().rstrip(";").rstrip()


class Section:
	def __init__(self, title, path, doc):
		self.title = title
		self.path = path
		self.doc = doc
		self.lines = []
		self.iterable = False


def parse(path, rel, sections, free):
	text = open(path, encoding="utf-8", errors="replace").read().splitlines()
	file_doc = []
	for line in text:
		if line.startswith("//"):
			file_doc.append(line[2:].strip())
		else:
			break
	stack = []   # (kind, name, depth inside, section)
	depth = 0
	pending_doc = []
	pending_open = None
	joined = ""
	for raw in text:
		line = raw.strip()
		code = strip_strings(raw).strip()
		plain = raw[:len(strip_strings(raw))].strip()
		if line.startswith("//"):
			pending_doc.append(line[2:].strip())
			continue
		if not code:
			pending_doc = []
			continue
		top = stack[-1] if stack else None
		in_class = top is not None and top[0] == "class" and depth == top[2]
		in_namespace = (top is None or top[0] == "namespace") and depth == (top[2] if top else 0)
		decl = None
		if in_class or in_namespace:
			attrs = []
			body = plain
			m = ATTRS.match(body)
			while m:
				attrs.append(m.group(1))
				body = body[m.end():]
				m = ATTRS.match(body)
			namespace = "::".join(s[1] for s in stack if s[0] in ("namespace", "class"))
			t = TYPE_DECL.match(re.sub(r"^(public|private)\s+", "", body))
			e = ENUM_DECL.match(re.sub(r"^(public|private)\s+", "", body))
			n = NAMESPACE.match(body)
			if n:
				pending_open = ("namespace", n.group(1), None)
			elif t and not body.rstrip().endswith(";") and re.match(r"^\s*<", body[t.end():]):
				pending_open = ("class", t.group(3), None)
			elif t and not body.rstrip().endswith(";"):
				name = t.group(3)
				template = re.sub(r"\s+", " ", t.group(1) or "").strip()
				title = (namespace + "::" if namespace else "") + name
				hidden = name.startswith("_") or (in_class and not body.startswith("public"))
				section = None if hidden else Section(title, rel, first_sentence(" ".join(pending_doc), 200) if pending_doc else "")
				if section and template:
					section.lines.append(template + " " + t.group(2) + " " + name)
				if section:
					sections.append(section)
				pending_open = ("class", name, section)
			elif e and not body.rstrip().endswith(";"):
				name = e.group(2)
				title = (namespace + "::" if namespace else "") + name
				section = Section(title, rel, first_sentence(" ".join(pending_doc), 200) if pending_doc else "")
				sections.append(section)
				pending_open = ("enum", name, section)
			elif in_class and re.match(r"^(template\s*<[^{]*?>\s*)?public\b(?!:)", body):
				sig = cut_body(re.sub(r"public\s+", "", body, count=1))
				ops = [OPERATOR.search(a).group(1) for a in attrs if OPERATOR.search(a)]
				doc = first_sentence(" ".join(pending_doc)) if pending_doc else first_sentence(comment_of(raw))
				if ops and ops[0] in ("begin", "end"):
					top[3] and setattr(top[3], "iterable", True)
				elif ops and ops[0] in ("native_bytes", "relocate", "trace", "copy", "delete", "move"):
					pass
				elif top[3] is not None:
					top[3].lines.append(("[%s] " % ops[0] if ops else "") + sig + ("   // " + doc if doc else ""))
			elif in_namespace and namespace and not body.startswith(("private", "using namespace")):
				f = FUNCTION.match(body)
				if body.startswith("using "):
					free.setdefault((namespace, rel), []).append(cut_body(body))
				elif f and f.group(3) not in KEYWORDS and not f.group(3).startswith("_") and "static " not in body[:f.start(3)]:
					sig = cut_body(body)
					doc = first_sentence(" ".join(pending_doc)) if pending_doc else first_sentence(comment_of(raw))
					free.setdefault((namespace, rel), []).append(sig + ("   // " + doc if doc else ""))
		if top is not None and top[0] == "enum" and depth == top[2] and top[3] is not None:
			values = [v.strip().split("=")[0].strip() for v in code.rstrip("}").split(",")]
			top[3].lines.extend(v for v in values if re.match(r"^\w+$", v))
		pending_doc = []
		for c in code:
			if c == "{":
				depth += 1
				if pending_open:
					stack.append((pending_open[0], pending_open[1], depth, pending_open[2]))
					pending_open = None
				else:
					stack.append(("block", "", depth, None))
			elif c == "}":
				if stack and stack[-1][2] == depth:
					stack.pop()
				depth -= 1
		if pending_open and code.endswith(";"):
			pending_open = None
	return file_doc


def attach_extensions(sections, free):
	"""Each extension method (`R Name(args) extends T;`) also listed in T's own section, as a member called on a T."""
	by_title = {s.title: s for s in sections}
	for (namespace, rel), funcs in free.items():
		for f in funcs:
			m = re.search(r"\)\s*(const\s+)?extends\s+([\w:<>]+)", f)
			if not m:
				continue
			target = by_title.get(m.group(2)) or by_title.get(namespace + "::" + m.group(2))
			if target is None:
				continue
			signature, _, doc = re.sub(r"\s*extends\s+[\w:<>]+", "", f, count=1).partition("   // ")
			target.lines.append("%s   // [extension, %s]%s" % (signature, rel, " " + doc if doc else ""))


def write_digest(root, out, title):
	"""The digest of the headers under `root` written to `out`; a line saying how big it came out."""
	sections = []
	free = {}
	file_docs = {}
	for folder, _, files in sorted(os.walk(root)):
		if os.path.basename(folder) == "Tests":
			continue
		for name in sorted(files):
			if name.endswith(".ch"):
				path = os.path.join(folder, name)
				rel = os.path.relpath(path, root).replace("\\", "/")
				file_docs[rel] = parse(path, rel, sections, free)
	attach_extensions(sections, free)
	lines = ["# " + title, "",
		"Every public type and function of the library's headers, one section each: the declarations as written (bodies cut), "
		"with the first sentence of each doc comment. [create] is a constructor (`T x = T(args);`), [delete] the destructor, "
		"[==], [index], [call] and the like the operators a method defines. A type marked range-for works in `for (T& x : c)`. "
		"The header named under each heading has the full comments.", ""]
	for s in sections:
		if not s.lines and not s.doc:
			continue
		if s.title.endswith("Iterator"):
			continue
		lines.append("## " + s.title)
		lines.append(s.path + (" - range-for" if s.iterable else "") + ". " + s.doc if s.doc else s.path + (" - range-for" if s.iterable else ""))
		lines.extend("    " + l for l in s.lines)
		lines.append("")
	for (namespace, rel), funcs in free.items():
		doc = first_sentence(" ".join(file_docs.get(rel, [])), 200)
		lines.append("## %s functions (%s)" % (namespace, rel))
		if doc:
			lines.append(doc)
		lines.extend("    " + f for f in funcs)
		lines.append("")
	text = "\n".join(lines) + "\n"
	text = text.encode("ascii", "replace").decode("ascii")
	os.makedirs(os.path.dirname(out), exist_ok=True)
	with open(out, "w", encoding="ascii", newline="\n") as f:
		f.write(text)
	return "%d sections, %d function groups, %d bytes" % (len(sections), len(free), len(text))


if __name__ == "__main__":
	print(write_digest(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "Library API digest"))
