// What a model server says of itself (docs/12 "Truth about the model's context window", docs/04 "Model profiles"):
// the models it serves and its context per slot - from the OpenAI-style /v1/models (vLLM's max_model_len, llama.cpp's
// meta.n_ctx) and llama.cpp's /props (the slot context, the slots) - and how long it took to answer. Settings'
// "Test connection" shows it; a turn learns the window of a profile that names none.

include "models.ch"

namespace Ai
{
	const int32 kProbeTimeoutMs = 5000;

	class ModelProbe
	{
		public bool ok;
		public Core::String error;
		public Core::String server;                   // "llama.cpp" when /props answered, else "OpenAI-compatible"
		public Core::Array<Core::String> models;      // the ids the server serves
		public bool modelServed;                      // the profile's model is one of them
		public int64 contextWindow;                   // per slot; 0 when the server does not say
		public int64 trainContext;
		public int64 slots;
		public int64 ms;

		public Core::String JsonText() const;
	}

	// Asks the profile's server; never longer than about `timeoutMs` per endpoint.
	ModelProbe ProbeModel(const ModelProfile& profile, int32 timeoutMs);
	// The parts of a probe each endpoint's answer gives.
	void ReadModelList(Core::StringView json, ModelProbe& probe);
	void ReadProps(Core::StringView json, ModelProbe& probe);
	// The server's root for endpoints beside the OpenAI ones: the endpoint without a trailing /v1.
	Core::String ServerRoot(Core::StringView endpoint);
}
