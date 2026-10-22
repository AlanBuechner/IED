// compare.h - capture compare (docs/22 "Compare"): two captures' events aligned by marker path and call, the state at
// an aligned pair diffed item by item, two images diffed texel by texel. It works on the replays' own answers (events,
// state, texture), so captures of different APIs compare too: D3D12 against Vulkan aligns markers by name and actions
// by kind.
#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace fb
{
	struct CompareEvent
	{
		u32 eid = 0;
		u32 parent = 0;          // the enclosing marker's EID; 0 at the top
		std::string kind;
		std::string name;
		bool hidden = false;     // state calls and EndEvent: left out of the alignment
	};

	// The rows of an events answer ([eid, parent, kind, hidden, last, queue, name]).
	std::vector<CompareEvent> ReadCompareEvents(const JsonValue& events);

	struct AlignedEvent
	{
		u32 a = 0, b = 0;        // EIDs; 0 when the event is only in the other capture
		u32 depth = 0;           // marker nesting
		bool changed = false;    // aligned, but its call or arguments differ
	};

	// Aligns the visible events marker by marker: the children of two aligned markers (or the top levels) by their
	// longest common subsequence of keys - a marker's name, an action's kind and call (its kind alone when the APIs
	// differ). A marker only one side has brings its whole subtree.
	std::vector<AlignedEvent> AlignEvents(const std::vector<CompareEvent>& a, const std::vector<CompareEvent>& b, bool sameApi);

	struct StateDifference
	{
		std::string path;        // "Output Merger / RTV 0"
		std::string a, b;        // the values; empty when the item is on one side only
	};

	// The items of two state answers whose values differ, found by group name and item label (repeated labels by their
	// order), with the command and the pipeline's name; `compared` gets how many items were compared. Object numbers in
	// names ("Fence #4") and resource ids are each capture's own and are not compared.
	std::vector<StateDifference> DiffState(const JsonValue& a, const JsonValue& b, u32& compared);

	struct ImageDifference
	{
		u32 changed = 0;         // pixels whose largest channel difference is above the threshold, or NaN on one side
		double largest = 0;      // the largest channel difference
		double mean = 0;         // the mean of each pixel's largest channel difference
		u32 box[4] = {};         // the changed pixels' bounds: x0, y0, x1, y1 (exclusive); zero when none changed
		u32 nan = 0;             // pixels NaN or Inf on one side only
	};

	// Two RGBA32F images of `width` x `height`; `diff` gets |a - b| per channel with alpha 1, NaN where one side is
	// NaN or Inf and the other is not.
	ImageDifference DiffImages(const float* a, const float* b, u32 width, u32 height, double threshold, std::vector<float>& diff);

	// `text` with object numbers ("Fence #4") made alike, for comparing names across captures.
	std::string WithoutObjectNumbers(const std::string& text);
}
