# The Linux build environment of the C4 compiler and its script JIT (C4 D-86): Debian 12 (TrueNAS SCALE's base) with
# LLVM 22 from apt.llvm.org, the release line the Windows build vendors (tools/setup/deps.toml). Nuke builds the C++
# projects in it with its C/C++ driver's Linux mode.
#
#   docker build -t ied-linux-builder tools/linux/builder
FROM mirror.gcr.io/library/debian:12

ARG LLVM=22
RUN apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends ca-certificates curl gnupg python3 git make \
	&& curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
	&& echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-${LLVM} main" \
		> /etc/apt/sources.list.d/llvm.list \
	&& apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends clang-${LLVM} lld-${LLVM} llvm-${LLVM} llvm-${LLVM}-dev libclang-${LLVM}-dev \
		libclang-cpp${LLVM}-dev liblld-${LLVM}-dev libstdc++-12-dev zlib1g-dev libzstd-dev libxml2-dev libz3-dev libssl-dev \
	&& rm -rf /var/lib/apt/lists/*

ENV LLVM_HOME=/usr/lib/llvm-${LLVM}
ENV PATH=/usr/lib/llvm-${LLVM}/bin:$PATH
