# What the browser's server checks need on Linux (docs/27 "The browser"): Debian 12 with its Chromium, the fonts a page
# renders with, Python for the checks, and a user to run them as - Chromium refuses to run as root, as on a desktop
# nobody runs it as root. Run it with --security-opt seccomp=unconfined (Chromium's own sandbox makes user namespaces)
# and --shm-size=1g:
#
#   docker build -t ied-linux-browser tools/linux/browser
#   docker run --rm --security-opt seccomp=unconfined --shm-size=1g -v <checkout>:/ied -w /ied ied-linux-browser \
#       python3 -I tools/server-checks/run_server_checks.py --server build/bin/Development-linux-x64/ied-server/ied-server -k test_browser
FROM mirror.gcr.io/library/debian:12

RUN apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends ca-certificates python3 libssl3 procps chromium fonts-dejavu-core fonts-liberation \
	&& rm -rf /var/lib/apt/lists/* \
	&& useradd --create-home --uid 1000 ied

USER ied
