// Bindings and events between plugins (docs/26 section 4, docs/02 "Bindings"): a server binding is a named function
// with JSON arguments and a JSON result that one plugin's code provides and any plugin calls - its code, its UI
// module (plugins.call) or an AI tool - and the protocol's events (docs/10) are what a plugin hears.

include "bedrock.ch"

namespace IED
{
	// A binding's answer: its result as JSON text, or why it failed.
	class Result
	{
		public Core::String json;
		public Core::String error;

		public bool Ok() const { return error.IsEmpty(); }
	}

	Result Answer(Core::StringView json);
	Result Refusal(Core::StringView error);

	alias Binding = func<Result(const Serialization::DomNode& args)>;

	// Calls the server binding `name` with `args` (JSON text) and waits for its provider, at most kBindingWaitMs. A
	// binding of the calling plugin itself runs at once.
	Result CallBinding(Core::StringView name, Core::StringView args);

	const int64 kBindingWaitMs = 30000;

	// A protocol event (docs/10) as it is published: its topic ("ws:w1"), its kind ("tree.changed") and its data.
	class Event
	{
		public Core::String topic;
		public Core::String kind;
		public Serialization::DomNode data;
	}

	alias EventHandler = func<void(const Event& event)>;
}
