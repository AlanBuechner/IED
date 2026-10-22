// A session's Edit and Replay output diff (docs/22 "Edit and Replay"): an event's output read with the captured shaders
// and again with the edits, diffed texel by texel as compare diffs two captures (WriteDifference).
#include "session.h"

namespace fb::replay
{
	void Session::EditDiff(const JsonValue& params, JsonWriter& out, std::vector<u8>& binary) {
		const u32 eid = (u32)params["eid"].Uint(0);
		u64 id = params["id"].Uint(0);
		if (!id) id = FirstTarget(Parsed([&](JsonWriter& w) { replay_->State(eid, w); }));
		if (!id) Fail("event %u writes no render target: name the texture (id)", eid);
		JsonWriter w;
		w.BeginObject().Uint("id", id).Uint("eid", eid).Uint("mip", params["mip"].Uint(0)).Uint("slice", params["slice"].Uint(0));
		w.Uint("maxSize", params["maxSize"].Uint(8192)).EndObject();
		const JsonValue request = Params(w);
		std::vector<u8> captured, edited;
		JsonValue before;
		replay_->UseEdits(false);
		try {
			before = Parsed([&](JsonWriter& t) { replay_->Texture(request, t, captured); });
		}
		catch (...) {
			replay_->UseEdits(true);
			throw;
		}
		replay_->UseEdits(true);
		const JsonValue after = Parsed([&](JsonWriter& t) { replay_->Texture(request, t, edited); });
		out.Uint("eid", eid).Uint("id", id);
		WriteDifference(params, before, captured, after, edited, "|captured - edited|", out, binary);
	}
}
