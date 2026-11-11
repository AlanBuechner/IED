// The Claude models the CLI offers (docs/04 "The Claude models"; owner, 2026-11-11: "make sure we properly support future
// models automaticly"): the CLI's initialize answer lists them - an alias or a full id each, with the model it resolves
// to, its name, what it is for and the efforts it takes - so IED adds to ai.models each current one it has not seen
// before: a model released after this build of IED as soon as the CLI offers it. A model is current when it is an alias
// (opus, sonnet, haiku, fable) or a full id newer than its family's alias resolves to (Fable 5.2 beside the fable alias
// at 5.1), or the newest of a family with no alias; the older versions the CLI keeps offering are left out. Each is named
// claude-<alias> or by its full id, with the CLI's name as its label and the efforts it takes when not every one. The
// models seen are kept in the plugin's state (claude-models.json), so one the owner took out of ai.models stays out.

include "bedrock.ch"

namespace Ai
{
	class ClaudeCatalogModel
	{
		public Core::String m_Value;          // what --model and set_model take: an alias or a full id
		public Core::String m_Resolved;       // the full id it resolves to
		public Core::String m_Label;          // "Fable 5.2"
		public Core::Array<Core::String> m_Efforts;   // supportedEffortLevels; empty: the CLI did not say
	}

	// The current models of the initialize answer's `models` (the order kept, "default" left out).
	Core::Array<ClaudeCatalogModel*> CurrentClaudeModels(const Serialization::DomNode& models);
	// ai.models (`modelsJson`) with the current models of `models` that are new - not in `seen`, not served by a profile
	// already (by name, or a claude-code profile with that model) - added, and a label given to each claude-code profile
	// of IED's (claude-<alias>) that has none. False when nothing changed. `seen` gets every current model's value.
	bool WithCatalogModels(Core::StringView modelsJson, const Serialization::DomNode& models, Core::Array<Core::String>& seen, Core::String& modelsOut, Core::Array<Core::String>& added);
	// The profile name of a catalog model: claude-<alias>, or the full id itself.
	Core::String ClaudeProfileName(Core::StringView value);
}
