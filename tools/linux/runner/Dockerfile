# What a C4 program built for Linux finds on an ordinary Debian 12 system (TrueNAS SCALE's base): glibc, the system's
# OpenSSL (TLS, C4 D-86) and its root certificates. The test suites run their Linux builds in it.
#
#   docker build -t ied-linux-runner tools/linux/runner
FROM mirror.gcr.io/library/debian:12

RUN apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends ca-certificates libssl3 procps gdb \
	&& rm -rf /var/lib/apt/lists/*
