# The Linux builder (tools/linux/builder) with clang's sanitizer runtimes, for C4Testing built with ThreadSanitizer
# (C4/assets/tsan_unit.py): lowering on several threads shares the model, and a race there is what no other suite finds.
#
#   docker build -t ied-linux-tsan tools/linux/tsan      (after ied-linux-builder)
FROM ied-linux-builder

ARG LLVM=22
RUN apt-get update -qq \
	&& apt-get install -y -qq --no-install-recommends libclang-rt-${LLVM}-dev \
	&& rm -rf /var/lib/apt/lists/*
