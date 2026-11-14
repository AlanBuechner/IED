# The IED config server (docs/35): ied-server --config-root, for the NAS (TrueNAS SCALE custom app, compose.yaml here).
# Its context is the folder tools/config-server/image.py makes from a Linux build: ied/ holds the server (ied-server,
# its interface ied-server.c4i and the script host C4Script.so, which compiles server plugins) and what it serves and
# loads (the web client, the built-in plugins, the host API's schemas).
#
#   python -I tools/config-server/image.py --bin build/bin/Development-linux-x64
#   docker build -t ied-config-server:latest build/config-server-image
FROM mirror.gcr.io/library/debian:12-slim

# glibc, libgcc_s and libstdc++ come with Debian; OpenSSL for TLS (--tls-identity) and the plugins' https requests; and
# what the script host's LLVM was built with (C4Script.so needs libz3, libxml2, libzstd and zlib).
RUN apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends ca-certificates libssl3 libz3-4 libxml2 libzstd1 zlib1g \
	&& rm -rf /var/lib/apt/lists/*

COPY ied/ /opt/ied/

# Everything it writes is under /data (the app's dataset on the NAS): config/ is the config root (settings.json,
# access.json, plugins/, skills/, server-plugins/, published/), app/ the server's own settings (its server plugins'
# own), state/ its logs and the plugins' state.
VOLUME ["/data"]
EXPOSE 8780
ENTRYPOINT ["/opt/ied/ied-server", "--config-root", "/data/config", "--app-data", "/data/app", "--data", "/data/state", \
	"--host", "0.0.0.0", "--port", "8780"]
