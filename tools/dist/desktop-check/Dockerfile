# A clean Debian 12 desktop for tools/dist/ied_release.py's Linux desktop check: a virtual display (Xvfb), a session bus,
# and the tools that look at windows and the applications menu - but not WebKitGTK, which the installer must bring.
FROM mirror.gcr.io/library/debian:12
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
		xvfb xauth x11-utils xdotool imagemagick dbus-x11 desktop-file-utils xdg-utils shared-mime-info fonts-dejavu-core \
		ca-certificates curl \
	&& rm -rf /var/lib/apt/lists/*
