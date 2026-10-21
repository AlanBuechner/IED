// Library: a loaded library (a DLL) and what it exports (D-63). It holds the module and frees it on delete, so it
// cannot be copied: move it with take(). A path with a folder is loaded from there and the libraries it needs are
// looked for beside it first; a bare name is searched for as the system searches. Opening one library twice gives
// the same module, which unloads when its last Library closes.
//     Platform::Library physics = Platform::Library("plugins/physics.dll");
//     if (!physics.IsOpen()) printf("%s\n", physics.Error());
//     funcptr<int32(int32)> step = (funcptr<int32(int32)>)physics.Find("Step");

include "Text/string.ch"

namespace Platform
{
	class Library
	{
		[operator("create")] public void Init() {}                 // closed
		[operator("create")] public void Init(const char* path);   // closed, with Error(), when it does not load
		[operator("delete")] public void Delete() { Close(); }

		public bool IsOpen() const { return handle != nullptr; }
		// The address of an exported function or variable; null when there is none or the library is closed.
		public void* Find(const char* symbol) const;
		public void Close();   // idempotent
		// Why the library did not load: the system's text and its error code. Empty when it loaded.
		public const char* Error() const { return error.CStr(); }

		private void* handle;   // the runtime's module, not GC memory
		private Core::String error;
	}
}
