// A session's compare requests (docs/22 "Compare"): `compareOpen` opens a second capture beside the first, with its own
// replay device; `compareEvents`, `compareState`, `compareTexture` and `compareTiming` answer from both replays' own
// answers through common/compare.h, so any two captures compare, D3D12 against Vulkan too. The texel difference is
// Edit and Replay's too (sessionedit.cpp).
#include "compare.h"
#include "session.h"

#include <cmath>
#include <unordered_map>

namespace fb::replay
{
	namespace
	{
		const char* const kStatus[] = {"same", "changed", "removed", "added"};

		// eid -> ms of a timing answer's rows ("events" or "markers": [eid, ms, start, end]).
		std::unordered_map<u32, double> Times(const JsonValue& rows) {
			std::unordered_map<u32, double> out;
			for (size_t i = 0; i < rows.Count(); ++i) out[(u32)rows.At(i).At(0).Uint()] = rows.At(i).At(1).Number();
			return out;
		}

		void Time(JsonWriter& out, const std::unordered_map<u32, double>& times, u32 eid) {
			auto found = eid ? times.find(eid) : times.end();
			if (found == times.end()) out.Null();
			else out.Double(found->second);
		}
	}

	u64 FirstTarget(const JsonValue& state) {
		const JsonValue& groups = state["groups"];
		for (size_t g = 0; g < groups.Count(); ++g) {
			const JsonValue& items = groups.At(g)["items"];
			for (size_t i = 0; i < items.Count(); ++i)
				if (items.At(i)["role"].Str() == "rt" && items.At(i).Has("resource")) return items.At(i)["resource"].Uint();
		}
		return 0;
	}

	void WriteDifference(const JsonValue& params, const JsonValue& ta, const std::vector<u8>& pixelsA, const JsonValue& tb, const std::vector<u8>& pixelsB, const char* view,
		JsonWriter& out, std::vector<u8>& binary) {
		const u32 width = (u32)ta["width"].Uint(), height = (u32)ta["height"].Uint();
		if (ta["srcWidth"].Uint() != tb["srcWidth"].Uint() || ta["srcHeight"].Uint() != tb["srcHeight"].Uint() || width != tb["width"].Uint() || height != tb["height"].Uint())
			Fail("the images differ in size: %llux%llu against %llux%llu", (unsigned long long)ta["srcWidth"].Uint(), (unsigned long long)ta["srcHeight"].Uint(),
				(unsigned long long)tb["srcWidth"].Uint(), (unsigned long long)tb["srcHeight"].Uint());
		const size_t bytes = (size_t)width * height * 16;
		if (pixelsA.size() < bytes || pixelsB.size() < bytes) Fail("a texture's pixels did not come back");
		const double threshold = params["threshold"].Number(1.0 / 255);
		std::vector<float> diff;
		const ImageDifference d = DiffImages((const float*)pixelsA.data(), (const float*)pixelsB.data(), width, height, threshold, diff);

		// The difference as the texture viewer reads an image, then what was compared.
		float lo[4] = {INFINITY, INFINITY, INFINITY, INFINITY}, hi[4] = {-INFINITY, -INFINITY, -INFINITY, -INFINITY};
		for (size_t i = 0; i < diff.size(); ++i)
			if (std::isfinite(diff[i])) lo[i % 4] = std::min(lo[i % 4], diff[i]), hi[i % 4] = std::max(hi[i % 4], diff[i]);
		out.String("name", "Difference");
		out.String("format", "difference");
		out.String("view", view);
		out.Uint("width", width).Uint("height", height).Uint("srcWidth", ta["srcWidth"].Uint()).Uint("srcHeight", ta["srcHeight"].Uint());
		out.Uint("step", ta["step"].Uint(1)).Uint("mip", params["mip"].Uint(0)).Uint("slice", params["slice"].Uint(0)).Uint("mips", 1).Uint("slices", 1);
		out.Uint("channels", 4).Bool("linear", true).Bool("depth", false).String("encoding", "rgba32f");
		out.BeginArray("min");
		for (u32 c = 0; c < 4; ++c) out.Double(std::isfinite(lo[c]) ? lo[c] : 0);
		out.EndArray();
		out.BeginArray("max");
		for (u32 c = 0; c < 4; ++c) out.Double(std::isfinite(hi[c]) ? hi[c] : 0);
		out.EndArray();
		out.Uint("nan", d.nan).Uint("inf", 0).Uint("negative", 0);
		out.Double("threshold", threshold);
		out.Uint("changed", d.changed).Uint("pixels", (u64)width * height);
		out.Double("largest", d.largest).Double("mean", d.mean);
		// The box in the source's pixels: a shown image smaller than the source has `step` of them to a pixel.
		const u64 step = ta["step"].Uint(1), right = ta["srcWidth"].Uint(), bottom = ta["srcHeight"].Uint();
		out.BeginArray("box").Uint(d.box[0] * step).Uint(d.box[1] * step).Uint(std::min<u64>(d.box[2] * step, right)).Uint(std::min<u64>(d.box[3] * step, bottom)).EndArray();
		out.Raw("a", ta.Dump());
		out.Raw("b", tb.Dump());
		binary.resize(diff.size() * 4);
		memcpy(binary.data(), diff.data(), binary.size());
	}

	ApiReplay& Session::Other() {
		if (!other_) Fail("no capture is open to compare with: send compareOpen first");
		return *other_;
	}

	void Session::CompareOpen(const JsonValue& params, JsonWriter& out) {
		const std::string path = params["capture"].Str();
		if (path.empty()) Fail("compareOpen needs the other capture's path");
		other_.reset();
		otherFile_.reset();
		auto file = std::make_unique<CaptureReader>();
		std::string error;
		if (!file->Open(Widen(path), error)) Fail("%s", error.c_str());
		std::unique_ptr<ApiReplay> replay = MakeReplay(file->GetApi());
		JsonWriter info;
		info.BeginObject();
		info.String("capture", path);
		info.Uint("bytes", file->FileSize());
		replay->Open(*file, params["options"], info);
		info.EndObject();
		otherFile_ = std::move(file);
		other_ = std::move(replay);
		out.Raw("other", info.Text());
		out.Bool("sameApi", SameApi());
	}

	void Session::CompareEvents(JsonWriter& out) {
		ApiReplay& other = Other();
		const std::vector<CompareEvent> a = ReadCompareEvents(Parsed([&](JsonWriter& w) { replay_->Events(w); }));
		const std::vector<CompareEvent> b = ReadCompareEvents(Parsed([&](JsonWriter& w) { other.Events(w); }));
		std::unordered_map<u32, const CompareEvent*> byA, byB;
		for (const CompareEvent& e : a) byA[e.eid] = &e;
		for (const CompareEvent& e : b) byB[e.eid] = &e;
		u32 counts[4] = {};
		out.Bool("sameApi", SameApi());
		out.BeginArray("columns").String("a").String("b").String("depth").String("status").String("kind").String("nameA").String("nameB").EndArray();
		out.BeginArray("rows");
		for (const AlignedEvent& row : AlignEvents(a, b, SameApi())) {
			const u32 status = !row.b ? 2 : !row.a ? 3 : row.changed ? 1 : 0;
			++counts[status];
			const CompareEvent* ea = row.a ? byA[row.a] : nullptr;
			const CompareEvent* eb = row.b ? byB[row.b] : nullptr;
			out.BeginArray().Uint(row.a).Uint(row.b).Uint(row.depth).String(kStatus[status]);
			out.String((ea ? ea : eb)->kind).String(ea ? ea->name : "").String(eb ? eb->name : "").EndArray();
		}
		out.EndArray();
		out.BeginObject("counts").Uint("same", counts[0]).Uint("changed", counts[1]).Uint("removed", counts[2]).Uint("added", counts[3]).EndObject();
	}

	void Session::CompareState(const JsonValue& params, JsonWriter& out) {
		ApiReplay& other = Other();
		const u32 eid = (u32)params["eid"].Uint(0), otherEid = (u32)params["otherEid"].Uint(0);
		const JsonValue a = Parsed([&](JsonWriter& w) { replay_->State(eid, w); });
		const JsonValue b = Parsed([&](JsonWriter& w) { other.State(otherEid, w); });
		u32 compared = 0;
		const std::vector<StateDifference> differences = DiffState(a, b, compared);
		out.Uint("eid", eid);
		out.Uint("otherEid", otherEid);
		out.Uint("compared", compared);
		out.BeginArray("differences");
		for (const StateDifference& d : differences) out.BeginArray().String(d.path).String(d.a).String(d.b).EndArray();
		out.EndArray();
	}

	void Session::CompareTexture(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		ApiReplay& other = Other();
		const u32 eid = (u32)params["eid"].Uint(0), otherEid = (u32)params["otherEid"].Uint(0);
		u64 id = params["id"].Uint(0), otherId = params["otherId"].Uint(0);
		if (!id) id = FirstTarget(Parsed([&](JsonWriter& w) { replay_->State(eid, w); }));
		if (!otherId) otherId = FirstTarget(Parsed([&](JsonWriter& w) { other.State(otherEid, w); }));
		if (!id || !otherId) Fail("event %u or %u writes no render target: name the textures to compare (id, otherId)", eid, otherEid);
		auto request = [&](u64 texture, u32 at) {
			JsonWriter w;
			w.BeginObject().Uint("id", texture).Uint("eid", at).Uint("mip", params["mip"].Uint(0)).Uint("slice", params["slice"].Uint(0));
			w.Uint("maxSize", params["maxSize"].Uint(8192)).EndObject();
			return Params(w);
		};
		std::vector<u8> pixelsA, pixelsB;
		const JsonValue ta = Parsed([&](JsonWriter& w) { replay_->Texture(request(id, eid), w, pixelsA); });
		const JsonValue tb = Parsed([&](JsonWriter& w) { other.Texture(request(otherId, otherEid), w, pixelsB); });
		out.Uint("eid", eid).Uint("otherEid", otherEid).Uint("id", id).Uint("otherId", otherId);
		WriteDifference(params, ta, pixelsA, tb, pixelsB, "|here - other|", out, binary);
	}

	void Session::CompareTiming(const JsonValue& params, JsonWriter& out, const Progress& progress) {
		ApiReplay& other = Other();
		auto half = [&](double from) {
			return Progress([&progress, from](const char* what, double fraction) {
				if (progress) progress(what, from + fraction / 2);
			});
		};
		const JsonValue ta = Parsed([&](JsonWriter& w) { replay_->Timing(params, w, half(0)); });
		const JsonValue tb = Parsed([&](JsonWriter& w) { other.Timing(params, w, half(0.5)); });
		const std::vector<CompareEvent> a = ReadCompareEvents(Parsed([&](JsonWriter& w) { replay_->Events(w); }));
		const std::vector<CompareEvent> b = ReadCompareEvents(Parsed([&](JsonWriter& w) { other.Events(w); }));
		const auto eventsA = Times(ta["events"]), eventsB = Times(tb["events"]), markersA = Times(ta["markers"]), markersB = Times(tb["markers"]);
		std::unordered_map<u32, const CompareEvent*> byA, byB;
		for (const CompareEvent& e : a) byA[e.eid] = &e;
		for (const CompareEvent& e : b) byB[e.eid] = &e;
		out.BeginArray("frameMs").Double(ta["frameMs"].Number()).Double(tb["frameMs"].Number()).EndArray();
		out.BeginArray("columns").String("a").String("b").String("depth").String("msA").String("msB").String("name").EndArray();
		const std::vector<AlignedEvent> rows = AlignEvents(a, b, SameApi());
		for (const char* list : {"markers", "events"}) {
			const bool markers = list[0] == 'm';
			const auto& timesA = markers ? markersA : eventsA;
			const auto& timesB = markers ? markersB : eventsB;
			out.BeginArray(list);
			for (const AlignedEvent& row : rows) {
				if (!timesA.count(row.a) && !timesB.count(row.b)) continue;
				const CompareEvent* e = row.a ? byA[row.a] : byB[row.b];
				out.BeginArray().Uint(row.a).Uint(row.b).Uint(row.depth);
				Time(out, timesA, row.a);
				Time(out, timesB, row.b);
				out.String(e->name).EndArray();
			}
			out.EndArray();
		}
	}
}
