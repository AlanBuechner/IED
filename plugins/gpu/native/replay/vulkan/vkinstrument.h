// vkinstrument.h - shader debugging by instrumented replay (docs/22 "Shader debugging"): a SPIR-V module rewritten so
// the chosen invocations - a pixel's fragments, one vertex, one compute thread - log every value they compute, in the
// order they compute it, into a storage buffer the replay reads back. A value is logged right where it is made, and an
// invocation that was not chosen logs into a scratch record instead, so no block is added: the module's control flow,
// phis and merges stay as they were.
#pragma once

#include "fb_base.h"

#include <string>
#include <vector>

namespace fb::replay::vulkan
{
	// A logged instruction: its word offset in the original module (its listing line), its result id (for a store, the
	// pointer stored to), and how its value reads.
	struct TraceSite
	{
		size_t offset = 0;
		u32 id = 0;
		u32 type = 0;              // 0 float, 1 int, 2 uint, 3 bool
		u32 components = 1;
		bool store = false;
	};

	struct TraceTarget
	{
		u32 model = 0;             // the entry point's execution model: 0 vertex, 4 fragment, 5 compute
		u32 x = 0, y = 0;          // a fragment's pixel
		u32 vertex = 0, instance = 0;
		u32 thread[3] = {};
		u32 set = 0;               // the descriptor set the trace buffer is bound at, binding 0
	};

	// The trace buffer, in 32-bit words: word 0 counts the words handed out (it starts at kTraceFirst); words 1 to 5 are
	// the scratch record; then a region per chosen invocation of kTraceRecords records, each the site's index + 1 and
	// four value words.
	constexpr u32 kTraceFirst = 16;
	constexpr u32 kTraceRecordWords = 5;
	constexpr u32 kTraceRecords = 16384;
	constexpr u32 kTraceRegions = 8;
	constexpr u32 kTraceWords = kTraceFirst + kTraceRegions * kTraceRecords * kTraceRecordWords;

	struct Instrumented
	{
		std::vector<u32> words;
		std::vector<TraceSite> sites;
		std::string problem;
	};

	// Rewrites `words` for the entry point named `entry` with `target.model`; false with `out.problem` when it cannot.
	bool InstrumentSpirv(const u32* words, size_t count, const std::string& entry, const TraceTarget& target, Instrumented& out);
}
