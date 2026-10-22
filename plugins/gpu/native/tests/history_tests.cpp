// Pixel History's account of what the APIs found (common/pixelhistory.cpp), with a pixel whose values the test makes
// up: a draw's verdict from its probes, the events left out, a write that keeps the value, and a value the events do
// not explain.
#include "testing.h"

#include "pixelhistory.h"

#include <map>

using namespace fb;
using namespace fb::replay;

namespace
{
	HistoryCandidate Draw(u32 eid, u64 covered, u64 facing, u64 depth, u64 passed, bool scissor = true) {
		HistoryCandidate c;
		c.eid = eid;
		c.draw = true;
		c.tests.measured = true;
		c.tests.variants = true;
		c.tests.scissor = scissor;
		c.tests.covered = covered;
		c.tests.facing = facing;
		c.tests.depth = depth;
		c.tests.passed = passed;
		return c;
	}

	HistoryCandidate Write(u32 eid, bool certain) {
		HistoryCandidate c;
		c.eid = eid;
		c.certain = certain;
		return c;
	}

	// A pixel whose value after each event is given; events not listed leave it as it was.
	JsonValue History(const std::vector<HistoryCandidate>& candidates, const std::map<u32, std::string>& values, u32 eid) {
		HistorySource source;
		source.after = [&](u32 at) {
			TexelValue texel;
			auto last = values.upper_bound(at);
			texel.text = last == values.begin() ? "0" : std::prev(last)->second;
			texel.raw = texel.text;
			return texel;
		};
		source.kind = [](u32 at) { return std::string("draw"); };
		source.name = [](u32 at) { return Format("event %u", at); };
		source.label = source.name;
		JsonWriter out;
		out.BeginObject();
		WritePixelHistory(candidates, source, eid, out);
		out.EndObject();
		JsonValue parsed;
		std::string error;
		JsonValue::Parse(out.Text().data(), out.Text().size(), parsed, error);
		return parsed;
	}

	std::string Results(const JsonValue& history) {
		std::string text;
		const JsonValue& events = history["events"];
		for (size_t i = 0; i < events.Count(); ++i) text += Format("%s%llu:%s", i ? " " : "", (unsigned long long)events.At(i)["eid"].Uint(), events.At(i)["result"].Str().c_str());
		return text;
	}
}

TEST(PixelHistoryJudgesEachDraw) {
	const JsonValue history = History({
		Write(2, true),                          // a clear to the value the pixel had
		Draw(5, 1, 1, 1, 1),                     // passed
		Draw(6, 0, 0, 0, 0),                     // covers nothing here: left out
		Draw(7, 1, 0, 0, 0),                     // culled
		Draw(8, 1, 1, 0, 0),                     // depth
		Draw(9, 1, 1, 1, 0),                     // stencil
		Draw(10, 1, 0, 0, 0, false),             // outside its scissor, whatever else
		Write(11, false),                        // a copy that read the pixel: left out
	}, {{5, "5"}}, 12);
	CHECK(Results(history) == "2:written 5:passed 7:culled 8:depth 9:stencil 10:scissor");
	const JsonValue& events = history["events"];
	CHECK(events.At(0)["changed"].Bool() == false && events.At(1)["changed"].Bool() == true);
	CHECK(events.At(1)["before"]["text"].Str() == "0" && events.At(1)["after"]["text"].Str() == "5");
	CHECK(events.At(3)["before"]["text"].Str() == "5" && events.At(3)["after"]["text"].Str() == "5");
	CHECK(events.At(1)["tests"]["covered"].Uint() == 1);
	CHECK(!history.Has("unattributed"));
	const std::string summary = history["summary"].Str();
	CHECK(summary.find("Written by 2 events") != std::string::npos);
	CHECK(summary.find("event 8) failed the depth test") != std::string::npos);
	CHECK(summary.find("and 1 more") != std::string::npos);
	CHECK(summary.find("After EID 12 it is 5.") != std::string::npos);
}

TEST(PixelHistorySaysWhatItCannotExplain) {
	// Nothing found, yet the pixel changed by event 4.
	const JsonValue history = History({Draw(3, 0, 0, 0, 0)}, {{4, "9"}}, 6);
	CHECK(history["events"].Count() == 0);
	CHECK(history["unattributed"].Bool());
	CHECK(history["summary"].Str().find("does not follow") != std::string::npos);
	// A draw whose probes could not run is read like any write.
	HistoryCandidate untested;
	untested.eid = 4;
	untested.draw = true;
	CHECK(Results(History({untested}, {{4, "9"}}, 6)) == "4:written");
}
