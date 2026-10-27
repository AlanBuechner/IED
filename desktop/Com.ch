// COM callbacks written in C4: WebView2 reports each completion and event through an object whose one method past
// IUnknown is Invoke(This, a, b). A Callback is such an object, laid out as COM expects (its vtable first), carrying a
// C4 function to run. Its owner keeps it (CallbackSet) for as long as WebView2 may call it.
//     Desktop::Callback* done = callbacks.Add(__uuidof(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler),
//         [self](void* error, void* controller) -> int32 { return self->ControllerReady(Desktop::Hresult(error), controller); });

include "Native.ch"

namespace Desktop
{
	// What a callback runs: the two arguments after This (an HRESULT arrives in the first as its low 32 bits).
	alias Invoked = func<int32(void*, void*)>;

	struct CallbackTable
	{
		public funcptr<int32(void*, const _GUID*, void**)> m_QueryInterface;
		public funcptr<uint32(void*)> m_AddRef;
		public funcptr<uint32(void*)> m_Release;
		public funcptr<int32(void*, void*, void*)> m_Invoke;
	}

	struct Callback
	{
		public CallbackTable* m_Table;   // first: what COM calls through
		public CallbackTable m_Own;
		public _GUID m_Iid;              // the interface it implements beside IUnknown
		public int32 m_References;
		public Invoked m_Invoked;
	}

	// The callbacks of one owner, alive while it is.
	class CallbackSet
	{
		public Callback* Add(const _GUID* iid, Invoked invoked);

		private Core::Array<Callback*> m_Items;
	}

	// An HRESULT passed in a pointer-sized argument.
	int32 Hresult(void* argument);
	bool SameGuid(const _GUID* a, const _GUID* b);
}
