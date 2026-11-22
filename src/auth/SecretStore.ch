// SecretStore (docs/11 "Secrets"): the values settings name with { "secret": "<name>" } and never hold themselves, kept in
// one file, the master's auth/secrets.json. On Windows each value is encrypted for the signed-in user by DPAPI
// (CryptProtectData, the secret's name as its entropy, so a value cannot be moved under another name): the file is of no
// use to another user or on another machine. On Linux, which has no DPAPI, the values are kept as the system keeps an
// SSH key or git's stored credentials: the auth/ folder readable by the user alone (0700, made so before anything is
// written in it) and the file too (0600) - not encrypted, so the file says which (`protection`). Elsewhere the store is
// not built and says so; { "env": ... } references remain there. Its own lock: the config server mirror reads it from its
// thread.

include "bedrock.ch"

namespace Ied
{
	// DATA_BLOB, as crypt32 lays it out.
	struct DpapiBlob
	{
		uint32 m_Size;
		uint8* m_Data;
	}

	// How this system keeps values: DPAPI (Windows), a file only its owner reads (Linux), or not at all.
	enum class SecretProtection { None, Dpapi, OwnerOnly }

	// One kept value as the file holds it: "dpapi" (sealed) or "file" (base64 of the value), and when it was set.
	class SecretEntry
	{
		public Core::String m_Kind;
		public Core::String m_Data;
		public int64 m_SetMs;
	}

	class SecretStore
	{
		[operator("create")] public void Init() {}
		[operator("create")] public void Init(Core::StringView file) { this.m_File = Core::String(file); }

		// Whether values can be kept here; false with why.
		public bool Available(Core::String& problem);
		// The value of `name`; false with `problem` (none is kept, the store is not built here, a value this user cannot
		// decrypt).
		public bool Get(Core::StringView name, Core::String& value, Core::String& problem);
		public bool Set(Core::StringView name, Core::StringView value, Core::String& problem);
		// `removed` says whether one was kept; false with `problem` when the file could not be written.
		public bool Remove(Core::StringView name, bool& removed, Core::String& problem);
		// {available, protection, problem?, secrets: [{name, setMs}]}: never a value. protection is "dpapi",
		// "owner-only" or "none".
		public Core::String ListJson();
		public Core::StringView File() const { return m_File.View(); }

	private:
		private bool Load(Core::String& problem);
		private bool Protect(Core::StringView name, Core::StringView value, Core::String& sealed, Core::String& problem);
		private bool Unprotect(Core::StringView name, Core::StringView sealed, Core::String& value, Core::String& problem);
		private bool Write(const Core::HashMap<Core::String, SecretEntry*>& entries, Core::String& problem);
		private Core::String SystemError(const char* what);

		private Core::String m_File;
		private Threading::Mutex m_Lock;
		private bool m_Loaded;
		private Core::String m_LoadProblem;
		private SecretProtection m_Protection;
		private Platform::Library m_Crypt;
		private Platform::Library m_Kernel;
		private Platform::Library m_Libc;
		private funcptr<int32(DpapiBlob*, void*, DpapiBlob*, void*, void*, uint32, DpapiBlob*)> m_Protect;
		private funcptr<int32(DpapiBlob*, void*, DpapiBlob*, void*, void*, uint32, DpapiBlob*)> m_Unprotect;
		private funcptr<void*(void*)> m_LocalFree;
		private funcptr<uint32()> m_LastError;
		private funcptr<int32(const char*, uint32)> m_Chmod;
	}

	// A secret's name: letters, digits, '.', '_' and '-', 1 to 64 of them.
	bool IsSecretName(Core::StringView name);
	// The longest value kept, in bytes.
	const int64 c_MostSecretBytes = 4096;
	// The modes the Linux store sets: its folder 0700, its file 0600.
	const uint32 c_SecretFolderMode = 448;
	const uint32 c_SecretFileMode = 384;
}
