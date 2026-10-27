// Pacing (docs/04 "Pacing"): how each place of a role's chain stands against the plan's usage now - a Claude model over its
// pace - the order a turn tries them in, and every chain of ai.groups with a Claude model, as the clients and the AI see it.
// A window IED reads as used up leaves no Claude model out: a turn asks Claude, and only Claude's own refusal moves it on
// (runclaude.cf ClaudeBlocked).

include "claudeaccount.ch"

namespace Ai
{
	enum class PlaceState { Use, Over }

	class ChainPlace
	{
		public Core::String model;
		public bool claude;
		public PaceLimit pace;
		public PlaceState state;
		public Core::String why;         // over its pace: "the week is at 1.85x pace, over its 1.00x"
		public double over;              // over its pace: the pace over its max (1.48 is 48% past it)
	}

	class RoleChain
	{
		public Core::String group;
		public Core::String role;
		public Core::Array<ChainPlace*> places;
		public Core::String now;         // the model a turn of the role uses now; empty when there is none
	}

	// How `profile`'s place, paced by `pace`, stands now; a `pinned` model is never over its pace. Inside the host lock.
	ChainPlace* PlaceNow(const ModelProfile& profile, const PaceLimit& pace, bool pinned, const ClaudeAccount& account, int64 nowMs);
	// The places of a turn's `candidates` now, each paced as `role` says; the first is never over its pace when it is the
	// chat's `pinned` model. Inside the host lock.
	Core::Array<ChainPlace*> PlacesOf(const Core::Array<ModelProfile*>& candidates, const RoleModel& role, Core::StringView pinned, const ClaudeAccount& account, int64 nowMs);
	// The places in the order a turn tries them: those it may use, in order, then those over their pace, the least over first
	// (the cheaper model, in a chain that steps down).
	Core::Array<ChainPlace*> TurnOrder(const Core::Array<ChainPlace*>& places);
	// The places a turn passes over to reach the first of `order` (TurnOrder's): those ahead of it in the chain that are over
	// their pace; every one not in use when `order` is empty. A routing note each; those behind it are not skipped.
	Core::Array<ChainPlace*> PassedOver(const Core::Array<ChainPlace*>& places, const Core::Array<ChainPlace*>& order);
	// Each role of each group of the ai.groups value whose chain has a Claude model, with its places now. Inside the host lock.
	Core::Array<RoleChain*> ClaudeChains(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, const ClaudeAccount& account, int64 nowMs);
	// [{group, role, now, places: [{model, claude, pace?: {max, target}, state: use | over, why?}]}]
	void WriteChains(IED::JsonWriter& json, const Core::Array<RoleChain*>& chains);
	// "claude-opus (over: the week is at 1.85x pace, over its 1.00x) > qwen - a turn uses qwen now", for the AI.
	Core::String ChainText(const RoleChain& chain);
}
