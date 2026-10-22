// Layouts (docs/10 "layout:<wsId>:<deviceClass>", docs/15): the layout document each device class of a workspace last
// stored, kept verbatim. A solution keeps them in .ied/local/layout/, a folder or a file in the app data's
// workspaces/<key>/layout/ (opening a folder writes nothing into it). layout.set is revision-checked; the client
// retries a conflict on the new revision (last writer wins, docs/11). Members run with the server lock held.

include "web/rpc.ch"

namespace Ied
{
	const int64 kLayoutMaxBytes = 262144;

	class StoredLayout
	{
		public Workspace* ws;
		public Core::String deviceClass;
		public int64 revision;
		public Core::String json;   // the document as JSON text; "null" until a client stores one
	}

	class Layouts
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Server* server);

		// A workspace opened: a topic per device class, with what its files hold.
		public void Opened(Workspace* ws);

	private:
		private StoredLayout* Find(const Workspace* ws, Core::StringView deviceClass) const;
		private StoredLayout* Load(Workspace* ws, Core::StringView deviceClass);
		private bool Save(const StoredLayout* layout, Core::StringView json, int64 revision) const;
		private FS::Path FileOf(const StoredLayout* layout) const;
		private Core::String Snapshot(const StoredLayout* layout) const;
		private Core::String SetMethod(Call& call, const Params& params);

		private Server* server;
		private Core::Array<StoredLayout*> layouts;
	}

	// The device classes of docs/15: "desktop", "tablet", "phone".
	bool IsDeviceClass(Core::StringView name);
	// The folder a workspace's layouts are kept in: `layout` in its local state (localstate.ch).
	FS::Path LayoutFolder(const Workspace* ws);
	Core::String LayoutTopic(const Workspace* ws, Core::StringView deviceClass);
	// {"revision": n, "layout": ...} as a layout file holds it; false when `text` is not one.
	bool ReadLayoutFile(Core::StringView text, int64& revision, Core::String& json);
}
