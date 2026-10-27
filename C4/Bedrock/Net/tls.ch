// TLS (D-84): TLS 1.2 and 1.3 on a TcpStream, through the runtime (C4Runtime/TlsRuntimeC.h; SChannel on Windows). A
// client secures a connected stream with TcpStream::StartTls(options), a server an accepted one with
// TcpStream::AcceptTls(identity); from then on the stream's Send and Receive carry TLS records. A client checks the
// server's certificate the way the system does - never skipped: it must chain to a root the system trusts (or to one
// of `extraRoots`, for a local or corporate authority), carry the host name, be within its validity and for server
// authentication, and not be revoked.
//     Net::TcpStream stream = Net::TcpStream::Connect(address, 5000);
//     Net::TlsClientOptions tls;
//     tls.hostName = Core::String("example.com");
//     if (!stream.StartTls(tls, 5000)) printf("%s\n", stream.Error());   // the certificate refused, the handshake failed

include "Container/array.ch"
include "Text/string.ch"
include "Text/stringview.ch"

namespace Net
{
	// What a secured stream speaks: TLS 1.3 where the system offers it (Windows 11), else 1.2.
	enum class TlsVersion { None = 0, Tls12 = 12, Tls13 = 13 }

	// What a client checks the server's certificate against.
	struct TlsClientOptions
	{
		public Core::String hostName;           // the name (or IP address) it must carry; also sent as SNI
		public Core::Array<uint8> extraRoots;   // CA certificates trusted besides the system's: PEM (any number) or one DER
	}

	// A TLS server's certificate and private key, from a PKCS #12 (.pfx) file's bytes. It owns them and releases them on
	// delete - a value local is RAII; it cannot be copied (move it with take()). Every stream it accepted must close
	// before it is released.
	class TlsIdentity
	{
		[operator("create")] public void Init() {}   // none
		[operator("delete")] public void Delete() { Release(); }

		// The identity in `bytes`, opened with `password` ("" for none); one not loaded, with Error(), when the bytes are
		// not PKCS #12, the password is wrong, or no certificate in it has its private key.
		public static TlsIdentity FromPkcs12(const void* bytes, int64 count, Core::StringView password);

		public bool IsLoaded() const { return slot != 0; }
		public const char* Error() const { return error.CStr(); }
		// The runtime's identity, -1 when none is loaded.
		public int64 Handle() const { return slot - 1; }
		public void Release();   // idempotent

		private int64 slot;   // the runtime's identity plus one
		private Core::String error;
	}
}
