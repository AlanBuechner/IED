// Capture compare (common/compare.cpp) on made-up answers: events aligned marker by marker - an added draw, a changed
// call, a marker on one side only, D3D12 against Vulkan, a level too long for the exact table - and state and images
// diffed.
#include "testing.h"

#include "compare.h"

#include <cmath>

using namespace fb;

namespace
{
	struct Events
	{
		std::vector<CompareEvent> list;
		u32 Add(u32 parent, const char* kind, const std::string& name, bool hidden = false) {
			CompareEvent e;
			e.eid = (u32)list.size() + 1;
			e.parent = parent;
			e.kind = kind;
			e.name = name;
			e.hidden = hidden;
			list.push_back(e);
			return e.eid;
		}
	};

	// The rows as "a/b/depth/status" text: "3/4/1/changed".
	std::vector<std::string> Rows(const std::vector<AlignedEvent>& rows) {
		std::vector<std::string> out;
		for (const AlignedEvent& r : rows)
			out.push_back(Format("%u/%u/%u/%s", r.a, r.b, r.depth, !r.b ? "removed" : !r.a ? "added" : r.changed ? "changed" : "same"));
		return out;
	}

	JsonValue Parse(const char* text) {
		JsonValue value;
		std::string error;
		JsonValue::Parse(text, strlen(text), value, error);
		return value;
	}
}

TEST(compare_aligns_events_marker_by_marker) {
	Events a, b;
	const u32 frameA = a.Add(0, "marker", "Frame");
	a.Add(frameA, "state", "SetPipelineState(Lit)", true);
	const u32 scene = a.Add(frameA, "marker", "Scene");
	a.Add(scene, "draw", "DrawInstanced(3, 1, 0, 0)");
	a.Add(scene, "draw", "DrawInstanced(6, 1, 0, 0)");
	const u32 bloom = a.Add(frameA, "marker", "Bloom");
	a.Add(bloom, "dispatch", "Dispatch(8, 8, 1)");
	a.Add(0, "present", "Present(1, 0)");

	const u32 frameB = b.Add(0, "marker", "Frame");
	const u32 sceneB = b.Add(frameB, "marker", "Scene");
	b.Add(sceneB, "draw", "DrawInstanced(3, 1, 0, 0)");
	b.Add(sceneB, "draw", "DrawIndexedInstanced(36, 1, 0, 0, 0)");
	b.Add(sceneB, "draw", "DrawInstanced(9, 1, 0, 0)");
	b.Add(0, "present", "Present(1, 0)");

	const std::vector<std::string> rows = Rows(AlignEvents(a.list, b.list, true));
	const std::vector<std::string> expected = {"1/1/0/same", "3/2/1/same", "4/3/2/same", "0/4/2/added", "5/5/2/changed", "6/0/1/removed", "7/0/2/removed", "8/6/0/same"};
	CHECK(rows == expected);
}

TEST(compare_aligns_actions_by_kind_across_apis) {
	Events a, b;
	const u32 pass = a.Add(0, "marker", "Shadows");
	a.Add(pass, "clear", "ClearDepthStencilView(DescriptorHeap #6[0], DEPTH, 1, 0, [0])");
	a.Add(pass, "draw", "DrawIndexedInstanced(36, 1, 0, 0, 0)");
	a.Add(pass, "draw", "DrawIndexedInstanced(36, 1, 0, 0, 0)");
	const u32 passB = b.Add(0, "marker", "Shadows");
	b.Add(passB, "draw", "vkCmdDrawIndexed(36, 1, 0, 0, 0)");
	b.Add(passB, "draw", "vkCmdDrawIndexed(12, 1, 0, 0, 0)");
	const std::vector<std::string> rows = Rows(AlignEvents(a.list, b.list, false));
	const std::vector<std::string> expected = {"1/1/0/same", "2/0/1/removed", "3/2/1/same", "4/3/1/changed"};
	CHECK(rows == expected);
}

TEST(compare_aligns_long_levels_greedily) {
	// Different first and last events leave a level of 2502 x 2502, past the exact table: the draws still align.
	Events a, b;
	a.Add(0, "copy", "CopyResource(A)");
	b.Add(0, "dispatch", "Dispatch(1, 1, 1)");
	for (u32 i = 0; i < 2500; ++i) {
		a.Add(0, "draw", Format("DrawInstanced(%u, 1, 0, 0)", i));
		b.Add(0, "draw", Format("DrawInstanced(%u, 1, 0, 0)", i == 1200 ? 7 : i));
	}
	a.Add(0, "present", "Present(1, 0)");
	b.Add(0, "resolve", "ResolveSubresource()");
	const std::vector<AlignedEvent> rows = AlignEvents(a.list, b.list, true);
	u32 same = 0, changed = 0;
	for (const AlignedEvent& r : rows) {
		same += r.a && r.b && !r.changed;
		changed += r.a && r.b && r.changed;
	}
	CHECK(same == 2499 && changed == 1);
	CHECK(rows.size() == 2504);
}

TEST(compare_diffs_state_items) {
	const JsonValue a = Parse(R"({"command":"DrawInstanced","pipeline":"Lit","groups":[
		{"name":"Output Merger","items":[{"label":"RTV 0","value":"RTV - HDR","resource":10},{"label":"Blend 0","value":"off"},{"label":"Blend 0","value":"off"}]},
		{"name":"Root Signature","items":[{"label":"[1] b1","value":"4 values","children":[{"label":"[0]","value":"1"},{"label":"[1]","value":"2"}]}]},
		{"name":"Fences","items":[{"label":"Wait","value":"Fence #4"}]}]})");
	const JsonValue b = Parse(R"({"command":"DrawInstanced","pipeline":"Lit","groups":[
		{"name":"Output Merger","items":[{"label":"RTV 0","value":"RTV - HDR","resource":77},{"label":"Blend 0","value":"off"},{"label":"Blend 0","value":"add"}]},
		{"name":"Root Signature","items":[{"label":"[1] b1","value":"4 values","children":[{"label":"[0]","value":"1"},{"label":"[1]","value":"3"}]}]},
		{"name":"Fences","items":[{"label":"Wait","value":"Fence #9"}]},
		{"name":"Stream Output","items":[]}]})");
	u32 compared = 0;
	const std::vector<StateDifference> d = DiffState(a, b, compared);
	CHECK(d.size() == 3);
	CHECK(d.size() == 3 && d[0].path == "Output Merger / Blend 0#2" && d[0].a == "off" && d[0].b == "add");
	CHECK(d.size() == 3 && d[1].path == "Root Signature / [1] b1 / [1]" && d[1].a == "2" && d[1].b == "3");
	CHECK(d.size() == 3 && d[2].path == "Stream Output" && d[2].a.empty() && d[2].b == "present");
	CHECK(compared == 2 + 7);
	CHECK(WithoutObjectNumbers("Signal(Fence #4, 5) on Queue #12") == "Signal(Fence #, 5) on Queue #");
}

TEST(compare_diffs_images) {
	const u32 w = 4, h = 3;
	std::vector<float> a(w * h * 4, 0.5f), b = a, diff;
	b[(1 * w + 2) * 4 + 0] = 0.75f;                 // (2, 1): red 0.25 apart
	b[(2 * w + 1) * 4 + 1] = 0.5f + 1.0f / 1024;    // (1, 2): below the threshold
	a[(0 * w + 3) * 4 + 2] = NAN;                   // (3, 0): NaN on one side
	const ImageDifference d = DiffImages(a.data(), b.data(), w, h, 1.0 / 255, diff);
	CHECK(d.changed == 2 && d.nan == 1);
	CHECK(d.largest == 0.25);
	CHECK(d.box[0] == 2 && d.box[1] == 0 && d.box[2] == 4 && d.box[3] == 2);
	CHECK(diff[(1 * w + 2) * 4] == 0.25f && diff[(1 * w + 2) * 4 + 3] == 1);
	CHECK(std::isnan(diff[(0 * w + 3) * 4 + 2]));
	CHECK(std::fabs(d.mean - (0.25 + 1.0 / 1024) / (w * h)) < 1e-9);
	std::vector<float> same(w * h * 4, NAN);
	const ImageDifference none = DiffImages(same.data(), same.data(), w, h, 0, diff);
	CHECK(none.changed == 0 && none.nan == 0 && none.box[2] == 0);
}
