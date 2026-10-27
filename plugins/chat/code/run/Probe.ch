// What a model server says of itself (docs/12 "Truth about the model's context window", docs/04 "Model profiles"):
// the models it serves and its context per slot - from the OpenAI-style /v1/models (vLLM's max_model_len, llama.cpp's
// meta.n_ctx) and llama.cpp's /props (the slot context, the slots) - and how long it took to answer. Settings'
// "Test connection" shows it; a turn learns the window of a profile that names none.

include "chats/Models.ch"

namespace Ai
{
	const int32 c_ProbeTimeoutMs = 5000;

	class ModelProbe
	{
		public bool m_Ok;
		public Core::String m_Error;
		public Core::String m_Server;                   // "llama.cpp" when /props answered, else "OpenAI-compatible"
		public Core::Array<Core::String> m_Models;      // the ids the server serves
		public bool m_ModelServed;                      // the profile's model is one of them
		public int64 m_ContextWindow;                   // per slot; 0 when the server does not say
		public int64 m_TrainContext;
		public int64 m_Slots;
		public bool m_Vision;                           // llama.cpp's /props says it takes images
		public int64 m_Ms;

		public Core::String JsonText() const;
	}

	// A GET's status and body; false with `error` when nothing usable came back in time. Shared by discovery and the
	// prefill watchdog's liveness question (`Step.ch`), which asks a server whether it is still reading.
	bool FetchText(Core::StringView url, const Core::Array<Core::String>& headers, int32 timeoutMs, int32& status, Core::String& body, Core::String& error);

	// Asks the profile's server; never longer than about `timeoutMs` per endpoint.
	ModelProbe ProbeModel(const ModelProfile& profile, int32 timeoutMs);
	// The parts of a probe each endpoint's answer gives.
	void ReadModelList(Core::StringView json, ModelProbe& probe);
	// The window of model `wanted` when the list gives one, else of the first that does.
	void ReadModelList(Core::StringView json, ModelProbe& probe, Core::StringView wanted);
	void ReadProps(Core::StringView json, ModelProbe& probe);
	// The server's root for endpoints beside the OpenAI ones: the endpoint without a trailing /v1.
	Core::String ServerRoot(Core::StringView endpoint);
}
