#include "pixelhistory.h"

namespace fb::replay
{
	namespace
	{
		// What a draw's tests say: the result's key and its words; no key when nothing of it reached the pixel or the
		// tests cannot tell.
		struct Verdict
		{
			const char* result = nullptr;
			const char* text = nullptr;
		};

		Verdict Judge(const DrawTests& t) {
			if (t.passed) return {"passed", "passed"};
			if (!t.variants || !t.covered) return {};
			if (!t.scissor) return {"scissor", "outside the scissor"};
			if (!t.facing) return {"culled", "culled: its primitives face away"};
			if (!t.depth) return {"depth", "failed the depth test"};
			return {"stencil", "failed the stencil test"};
		}

		bool Same(const TexelValue& a, const TexelValue& b) { return a.raw.empty() ? a.text == b.text : a.raw == b.raw; }

		void WriteValue(JsonWriter& out, const char* key, const TexelValue& value) {
			out.BeginObject(key);
			WriteTexel(value, out);
			out.EndObject();
		}

		struct Noted
		{
			u32 eid;
			std::string what;
		};

		std::string Listed(const std::vector<Noted>& notes, size_t most) {
			std::string text;
			for (size_t i = 0; i < notes.size() && i < most; ++i) text += (i ? ", " : "") + notes[i].what;
			if (notes.size() > most) text += Format(" and %zu more", notes.size() - most);
			return text;
		}
	}

	void WritePixelHistory(const std::vector<HistoryCandidate>& candidates, const HistorySource& source, u32 eid, JsonWriter& out) {
		TexelValue before = source.after(0);
		WriteValue(out, "start", before);
		std::vector<Noted> writes, failures;
		out.BeginArray("events");
		for (const HistoryCandidate& candidate : candidates) {
			if (candidate.eid > eid) break;
			// A draw whose queries could not run is read like any other write.
			const bool tested = candidate.draw && candidate.tests.measured;
			const Verdict verdict = tested ? Judge(candidate.tests) : Verdict{"written", "written"};
			if (!verdict.result) continue;
			const bool wrote = !tested || candidate.tests.passed;
			const TexelValue after = wrote ? source.after(candidate.eid) : before;
			if (!tested && !candidate.certain && Same(after, before)) continue;
			out.BeginObject();
			out.Uint("eid", candidate.eid);
			out.String("kind", source.kind(candidate.eid));
			out.String("name", source.name(candidate.eid));
			out.String("result", verdict.result);
			out.String("text", verdict.text);
			if (tested) out.Uint("samples", candidate.tests.passed);
			if (tested && candidate.tests.variants)
				out.BeginObject("tests").Uint("covered", candidate.tests.covered).Uint("facing", candidate.tests.facing).Uint("depth", candidate.tests.depth).Uint("passed", candidate.tests.passed).EndObject();
			WriteValue(out, "before", before);
			WriteValue(out, "after", after);
			out.Bool("changed", !Same(before, after));
			out.EndObject();
			const std::string what = Format("EID %u (%s)", candidate.eid, source.label(candidate.eid).c_str());
			if (wrote) writes.push_back({candidate.eid, what});
			else failures.push_back({candidate.eid, what + " " + verdict.text});
			before = after;
		}
		out.EndArray();
		const TexelValue end = source.after(eid);
		WriteValue(out, "end", end);
		const bool unattributed = !Same(end, before);
		if (unattributed) out.Bool("unattributed", true);
		std::string summary;
		if (writes.empty() && failures.empty()) summary = Format("Nothing up to EID %u wrote this pixel or tried to; it keeps its value from the frame's start.", eid);
		else {
			if (!writes.empty()) summary = Format("Written by %zu event%s: %s.", writes.size(), writes.size() == 1 ? "" : "s", Listed(writes, 4).c_str());
			else summary = "No event wrote it.";
			if (!failures.empty()) summary += " Kept out: " + Listed(failures, 3) + ".";
		}
		summary += Format(" After EID %u it is %s.", eid, end.text.c_str());
		if (unattributed) summary += " Something this history does not follow changed it too: the events above leave a different value.";
		out.String("summary", summary);
	}
}
