"""Fetches a URL for IED's web fetch plugin (docs/29 "Transport") the way a browser would: curl_cffi impersonating a
browser's TLS and HTTP/2 fingerprint and headers, which many bot checks let through where plain curl is refused. The
body goes to standard output; the report - status, type, final URL, exit code, message - goes to standard error after a
marker, as the plugin reads curl's --write-out. Run by the private Python:

	python -I fetch.py --packages DIR --url URL [--max-bytes N] [--timeout S] [--impersonate chrome]
"""

import argparse
import sys

MARKER = "IED-WEBFETCH"
TOO_LARGE = 63          # curl's code for a body past its limit, which the plugin reads the same way
FAILED = 1
CHUNK = 65536


def parse(argv):
	parser = argparse.ArgumentParser(prog="fetch.py", description=__doc__)
	parser.add_argument("--packages", required=True, help="the folder curl_cffi is installed in")
	parser.add_argument("--url", required=True)
	parser.add_argument("--max-bytes", type=int, default=5 * 1024 * 1024)
	parser.add_argument("--timeout", type=float, default=30)
	parser.add_argument("--impersonate", default="chrome", help="chrome, edge, safari or firefox")
	return parser.parse_args(argv)


def one_line(text):
	return " ".join(str(text).split())


def fetch(options, out):
	"""(status, content type, final URL, exit code, message), the body written to `out` as it arrives."""
	from curl_cffi import requests
	if not options.url.lower().startswith(("http://", "https://")):
		return 0, "", options.url, FAILED, "only http and https addresses"
	with requests.Session(impersonate=options.impersonate, max_redirects=8) as session:
		response = session.get(options.url, timeout=options.timeout, allow_redirects=True, stream=True)
		status, kind, final = response.status_code, response.headers.get("content-type", ""), str(response.url)
		received = 0
		for chunk in response.iter_content(chunk_size=CHUNK):
			room = options.max_bytes - received
			if len(chunk) > room:
				out.write(chunk[:room])
				return status, kind, final, TOO_LARGE, "Maximum file size exceeded"
			out.write(chunk)
			received += len(chunk)
		return status, kind, final, 0, ""


def main(argv):
	options = parse(argv)
	sys.path.insert(0, options.packages)
	out = sys.stdout.buffer
	try:
		status, kind, final, code, message = fetch(options, out)
	except Exception as error:  # every failure becomes the report's message
		status, kind, final, code, message = 0, "", options.url, FAILED, one_line(error) or type(error).__name__
	out.flush()
	sys.stderr.write("\n%s\t%d\t%s\t%s\t%d\t%s\n" % (MARKER, status, one_line(kind), one_line(final), code, one_line(message)))
	sys.stderr.flush()
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
