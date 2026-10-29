// A session's Edit and Replay output diff (docs/22 "Edit and Replay"): an event's output read with the captured shaders
// and again with the edits, diffed texel by texel as compare diffs two captures (WriteDifference). A volume's or an
// array's output, when no slice is asked for, is scanned slice by slice and the slice that changed most is shown.
#include "compare.h"
#include "session.h"

#include <algorithm>

namespace fb::replay
{
	namespace
	{
		const u32 kScanSize = 512;      // the scan's images are at most this size: a volume's slices kept in memory
		const u32 kMaxScanned = 256;    // slices scanned at most

		struct SliceScan
		{
			u32 slice = 0;      // the one that changed most
			u32 changed = 0;    // slices with any change
			u32 scanned = 1;
		};
	}

	void Session::EditDiff(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		u64 id = params["id"].Uint(0);
		if (!id) id = FirstTarget(Parsed([&](JsonWriter& w) { replay_->State(eid, w); }));
		if (!id) Fail("event %u writes no render target, depth target or storage texture to compare", eid);
		auto request = [&](u32 slice, u64 maxSize) {
			JsonWriter w;
			w.BeginObject().Uint("id", id).Uint("eid", eid).Uint("mip", params["mip"].Uint(0)).Uint("slice", slice);
			w.Uint("maxSize", maxSize).EndObject();
			return Params(w);
		};
		auto read = [&](bool edits, u32 slice, u64 maxSize, std::vector<u8>& pixels) {
			replay_->UseEdits(edits);
			return Parsed([&](JsonWriter& t) { replay_->Texture(request(slice, maxSize), t, pixels); });
		};
		const double threshold = params["threshold"].Number(1.0 / 255);
		const u64 maxSize = params["maxSize"].Uint(8192);
		SliceScan scan;
		scan.slice = (u32)params["slice"].Uint(0);
		JsonValue before, after;
		std::vector<u8> captured, edited;
		try {
			before = read(false, scan.slice, maxSize, captured);
			const u32 slices = (u32)before["slices"].Uint(1);
			if (!params.Has("slice") && slices > 1) {
				// Every slice small, the captured side kept, then each edited slice against it.
				const u64 small = std::min<u64>(maxSize, kScanSize);
				scan.scanned = std::min(slices, kMaxScanned);
				std::vector<std::vector<u8>> kept(scan.scanned);
				std::vector<JsonValue> shapes(scan.scanned);
				for (u32 s = 0; s < scan.scanned; ++s) shapes[s] = read(false, s, small, kept[s]);
				u32 most = 0;
				double largest = 0;
				std::vector<u8> pixels;
				std::vector<float> scratch;
				for (u32 s = 0; s < scan.scanned; ++s) {
					const JsonValue shape = read(true, s, small, pixels);
					const u32 width = (u32)shape["width"].Uint(), height = (u32)shape["height"].Uint();
					const size_t bytes = (size_t)width * height * 16;
					if (width != shapes[s]["width"].Uint() || height != shapes[s]["height"].Uint() || pixels.size() < bytes || kept[s].size() < bytes) continue;
					const ImageDifference d = DiffImages((const float*)kept[s].data(), (const float*)pixels.data(), width, height, threshold, scratch);
					if (d.changed) ++scan.changed;
					if (d.changed > most || (d.changed == most && d.largest > largest)) {
						most = d.changed;
						largest = d.largest;
						scan.slice = s;
					}
				}
				if (scan.slice != 0) before = read(false, scan.slice, maxSize, captured);
			}
		}
		catch (...) {
			replay_->UseEdits(true);
			throw;
		}
		after = read(true, scan.slice, maxSize, edited);
		JsonWriter shown;
		shown.BeginObject().Double("threshold", threshold).Uint("mip", params["mip"].Uint(0)).Uint("slice", scan.slice).EndObject();
		out.Uint("eid", eid).Uint("id", id);
		out.Uint("slicesScanned", scan.scanned).Uint("slicesChanged", scan.changed);
		WriteDifference(Params(shown), before, captured, after, edited, "|captured - edited|", out, binary);
	}
}
