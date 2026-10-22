// Pacing (docs/04 "Pacing"): how each place of a role's chain stands against the plan's usage now - a Claude model over its
// pace, one used up - the order a turn tries them in, and every chain of ai.groups with a Claude model, as the clients and the
// AI see it.

include "claudeaccount.ch"

namespace Ai
{
	enum class PlaceState { Use, Over, UsedUp }

	class ChainPlace
	{
		public Core::String model;
		public bool claude;
		public PaceLimit pace;
		public PlaceState state;
		public Core::String why;         // over or used up: "the week is at 1.85x pace, over its 1.00x"
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
	// The places in the order a turn tries them: those it may use, in order, then those over their pace, the least over first
	// (the cheaper model, in a chain that steps down); used up left out.
	Core::Array<ChainPlace*> TurnOrder(const Core::Array<ChainPlace*>& places);
	// Each role of each group of the ai.groups value whose chain has a Claude model, with its places now. Inside the host lock.
	Core::Array<RoleChain*> ClaudeChains(const Core::Array<ModelProfile*>& models, Core::StringView groupsJson, const ClaudeAccount& account, int64 nowMs);
	// [{group, role, now, places: [{model, claude, pace?: {max, target}, state: use | over | used_up, why?}]}]
	void WriteChains(IED::JsonWriter& json, const Core::Array<RoleChain*>& chains);
	// "claude-opus (over: the week is at 1.85x pace, over its 1.00x) > qwen - a turn uses qwen now", for the AI.
	Core::String ChainText(const RoleChain& chain);
}
