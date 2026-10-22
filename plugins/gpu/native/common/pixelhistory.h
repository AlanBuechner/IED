// pixelhistory.h - Pixel History (docs/22 "Pixel History"): every event up to one event that wrote, or tried to write,
// one pixel of a texture - the value before and after it and, for a draw, the test that kept its fragments out - and a
// summary in plain words. Each API finds the candidate events and runs their draws' tests (d3d12/history.cpp,
// vulkan/vkhistory.cpp); what they found is told here.
#pragma once

#include "json.h"
#include "texels.h"

#include <functional>

namespace fb::replay
{
	// A draw's samples at the pixel, from occlusion queries with the scissor clamped to the pixel: with depth, stencil
	// and culling off and no scissor but the pixel (covered), then with the pipeline's culling (facing), with its depth
	// test (depth), and the draw as it is (passed). The first three run as copies of the draw with every write off.
	struct DrawTests
	{
		bool scissor = true;       // the pixel is inside the draw's scissor
		bool measured = false;     // the draw's own query ran
		bool variants = false;     // the copies ran too (a pipeline whose shaders write storage gets none)
		u64 covered = 0;
		u64 facing = 0;
		u64 depth = 0;
		u64 passed = 0;
	};

	struct HistoryCandidate
	{
		u32 eid = 0;
		bool draw = false;
		bool certain = false;      // it writes the pixel for sure (a clear, a copy into it): shown even when the value stays
		DrawTests tests;
	};

	// What the history asks of the API: the pixel after an event (0: at the frame's start), an event's kind and name.
	struct HistorySource
	{
		std::function<TexelValue(u32 eid)> after;
		std::function<std::string(u32 eid)> kind;
		std::function<std::string(u32 eid)> name;
	};

	// Writes start, events (each {eid, kind, name, result, text, samples?, tests?, before, after, changed}), end, summary, and
	// `unattributed` when the value at `eid` differs from what the events found leave.
	void WritePixelHistory(const std::vector<HistoryCandidate>& candidates, const HistorySource& source, u32 eid, JsonWriter& out);

	// Whether pixel (x, y) is inside the rectangle (left, top, right, bottom).
	inline bool Inside(i64 x, i64 y, i64 left, i64 top, i64 right, i64 bottom) { return x >= left && x < right && y >= top && y < bottom; }
}
