// Params: a request's params object read field by field. A missing required field, or one of the wrong type, throws
// RpcError bad_request, as every method would answer it.
//     Core::String path = params.Str("path");
//     int64 lines = params.Int("lines", 5);

include "rpcerror.ch"

namespace Ied
{
	class Params
	{
		[operator("create")] public void Init() {}
		// `object` must stay alive while the params are read: an invalid or non-object node reads as no fields.
		[operator("create")] public void Init(Serialization::DomNode object) { this.object = object; }

		public bool Has(const char* name) const { return Field(name).IsValid(); }
		public Core::String Str(const char* name) const;
		public Core::String Str(const char* name, Core::StringView fallback) const;
		public int64 Int(const char* name) const;
		public int64 Int(const char* name, int64 fallback) const;
		public bool Bool(const char* name, bool fallback) const;
		// A required array.
		public Serialization::DomNode List(const char* name) const;
		// Any value, invalid when absent.
		public Serialization::DomNode Field(const char* name) const;

	private:
		private Serialization::DomNode Required(const char* name, Serialization::DomKind kind, const char* label) const;

		private Serialization::DomNode object;
	}

	// A node's value as JSON text.
	Core::String NodeJson(const Serialization::DomNode& node);
}
