// The browser (docs/27 "The browser"), the ied.browser plugin's server code: web pages that live on the server, shared
// by every client and the AI. One headless Edge for the server (engine), one browser context per workspace, a page a
// CDP target loaded when someone looks at it. The browser:<wsId> topic lists a workspace's pages, page:<pageId> carries
// one page's state, console, failed requests and the actions done in it; frames are pulled (browser.frame).
//
// Threads: protocol methods run on their requests' threads and AI tools on the plugin's; the CDP reader hands events to
// Event (which never waits on the browser); a worker runs what events need asked of the browser and stops idle
// screencasts. Page fields change inside Atomically with their events; `starting` serializes starting the engine and
// loading pages; frames and load counts are `frames`'.

include "engine.ch"
include "page.ch"

namespace Browser
{
	const int64 kFrameWaitNs = 10000000000;
	const int64 kScreencastIdleNs = 5000000000;
	const int64 kWorkerTickNs = 1000000000;
	const int64 kLoadWaitNs = 30000000000;
	const int64 kScreencastRetryNs = 250000000;
	const int64 kDefaultUnloadSeconds = 600;

	// Added to every document of a page: tells the server the top document's title as it changes (headless browsers
	// do not report a script's document.title), through the page's __iedPageTitle binding.
	const char* kTitleReporter = "(() => {\n"
		"\tif (window !== window.top) return;\n"
		"\tconst report = () => { try { window.__iedPageTitle(document.title); } catch (e) {} };\n"
		"\tconst title = Object.getOwnPropertyDescriptor(Document.prototype, 'title');\n"
		"\tif (title && title.set) Object.defineProperty(Document.prototype, 'title', { configurable: true, enumerable: title.enumerable, get: title.get,\n"
		"\t\tset(value) { title.set.call(this, value); report(); } });\n"
		"\tconst watch = () => { if (document.head) new MutationObserver(report).observe(document.head, { subtree: true, childList: true, characterData: true }); report(); };\n"
		"\tif (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', watch); else watch();\n"
		"})();\n";

	class Browsers
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(IED::PluginContext* context) { this.context = context; }

		public void Start();
		public void Opened(const IED::Workspace& ws);

		// A new page of workspace `wsId` at `url`, loaded; throws RpcError for a URL it may not open. `chatId`: the chat
		// whose AI opens it, which then lists it while it is loaded ("" for none).
		public Page* Open(Core::StringView wsId, Core::StringView url, int32 width, int32 height, int32 scalePercent, Core::StringView opener,
			Core::StringView chatId);
		// The page, or RpcError not_found.
		public Page* Find(Core::StringView pageId);
		// Loads the page into the browser when it is not; throws RpcError unavailable when the browser cannot run.
		public void Load(Page* page);
		public void Navigate(Page* page, Core::StringView url);
		public void Back(Page* page, int32 step);
		public void Reload(Page* page);
		public void StopLoading(Page* page);
		public void Resize(Page* page, int32 width, int32 height, int32 scalePercent);
		public void Close(Page* page);
		// Waits (at most `ns`) for a load event after `loadsBefore`; false when none came.
		public bool AwaitLoad(Page* page, int64 loadsBefore, int64 ns);
		public int64 Loads(Page* page);
		// The newest frame after `after`, waiting up to kFrameWaitNs; JSON for browser.frame.
		public Core::String Frame(Page* page, int64 after);
		// A command to the page's session; RpcError unavailable when it is not loaded or the browser fails.
		public CdpReply Call(Page* page, Core::StringView method, Core::StringView paramsJson);
		// An action event on the page's topic, for viewers (docs/27 "Watching the AI").
		public void Acted(Page* page, Core::StringView actionJson);
		public Core::String ListJson(Core::StringView wsId) const;
		// The workspace's URL rules: http(s), about:blank, file: inside its folder; a bare host gets a scheme. Throws
		// RpcError bad_request or forbidden.
		public Core::String Allowed(Core::StringView wsId, Core::StringView url) const;

		// The CDP reader's.
		public void Event(const CdpEvent& event);
		public void EngineClosed(Core::StringView why);

		public IED::PluginContext* context;
		public Engine engine;
		public Core::Array<Page*> pages;
		public Core::HashMap<Core::String, Page*> bySession;
		public Core::HashMap<Core::String, Page*> byTarget;
		public Core::HashMap<Core::String, Core::String> contexts;   // wsId -> browserContextId (under `starting`)

	private:
		private void EnsureEngine();
		private Core::String ContextFor(Core::StringView wsId);
		private void Viewport(Page* page);
		private void Attach(Page* page, Core::StringView targetId, Core::StringView navigateTo);
		private void Adopt(Page* parent, Core::StringView targetId, Core::StringView url);
		private bool StartScreencast(Page* page);
		private void Changed(IED::Events& events, Page* page);
		// The chat whose AI opened `page` lists it while it is loaded, under its title (chat.resource, docs/04 "What a
		// chat has open"); told on the worker, in order, only when that changes. In a change (the server's lock held).
		private void TellChat(Page* page, bool open);
		private void Persist(Core::StringView wsId);
		private void Later(func<void()> job);
		private void WorkLoop();
		private void StopIdleScreencasts();
		// Pages nobody looked at or used for browser.unloadAfterSeconds leave the browser (still listed; they load again
		// when next looked at or used); the browser stops once no page was loaded for that long.
		private void UnloadIdlePages();
		private int64 UnloadAfterNs();
		// The page's title (headless browsers do not always tell when it changes) and history flags, asked of the page.
		private void RefreshState(Page* page);
		private void PageEvent(Page* page, const CdpEvent& event);
		private void ConsoleEvent(Page* page, const CdpEvent& event);
		private void NetworkEvent(Page* page, const CdpEvent& event);
		private void FrameEvent(Page* page, const CdpEvent& event);
		private void TitleReported(Page* page, const CdpEvent& event);
		private void Opened(const CdpEvent& event);
		private void Unloaded(Page* page);

		private Threading::Mutex starting;
		private Threading::Mutex frames;
		private Threading::ConditionVariable frameArrived;
		private Threading::Mutex jobs;
		private Threading::ConditionVariable jobAdded;
		private Core::Array<func<void()>> queue;
		private int64 idleSinceNs;    // the worker's: since when no page has been loaded
	}

	Core::String BrowserTopic(Core::StringView wsId);
	// The browser.* methods on the plugin's context.
	void RegisterMethods(Browsers* browsers);
	// The browser.* bindings (docs/27 "AI tools"), which the AI's tools and other plugins call.
	void ProvideBindings(Browsers* browsers);
}
