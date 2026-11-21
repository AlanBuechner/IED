// ConfigAccess (docs/35 "The API", docs/11 "Config server tokens"): who may use a config server's API. Its folder's
// access.json lists the tokens - {version: 1, tokens: [{name, role, sha256, createdMs}]} - by their SHA-256 alone: a
// token is 32 random bytes as 64 hex digits, shown once when it is made (`ied-server --config-root <folder>
// --config-token reader|admin <name>`). A reader (a client IED) reads; an admin (the owner's deploy tooling) also writes.

include "bedrock.ch"

namespace Ied
{
	enum class ConfigRole { None, Reader, Admin }

	class ConfigToken
	{
		public Core::String m_Name;
		public ConfigRole m_Role;
		public Core::String m_Digest;   // the token's SHA-256, 64 hex digits
		public int64 m_CreatedMs;
	}

	class ConfigAccess
	{
		[operator("create")] public void Init() {}

		// Reads `file`; false with `error` when it is there but does not read (no file: no tokens).
		public bool Load(const FS::Path& file, Core::String& error);
		// The role `token` holds and its name; None for any other text. Compares digests in constant time.
		public ConfigRole Check(Core::StringView token, Core::String& name) const;
		public int64 Count() const { return m_Tokens.Count(); }
		// Adds `role` named `name` for the token whose digest is `digest`, in place of one of that name.
		public void Put(Core::StringView name, ConfigRole role, Core::StringView digest, int64 createdMs);
		// {version, tokens: [...]}: what access.json holds.
		public Core::String Json() const;
		// The role of the token named `name`; None when there is none.
		public ConfigRole RoleOf(Core::StringView name) const;
		// Takes the token named `name` out; false when there was none.
		public bool Remove(Core::StringView name);
		public int64 AdminCount() const;
		// [{name, role, createdMs}]: never a digest.
		public Core::String ListJson() const;

		private Core::Array<ConfigToken*> m_Tokens;
	}

	const char* ConfigRoleName(ConfigRole role);
	// "reader" or "admin"; None for anything else.
	ConfigRole ConfigRoleOf(Core::StringView name);
	// A new token for `name` with `role`, its digest kept in `root`/access.json (one of the same name is replaced, so
	// making a token again revokes the old one): the token, or false with `error`.
	bool MintConfigToken(const FS::Path& root, ConfigRole role, Core::StringView name, Core::String& token, Core::String& error);
	// The token named `name` taken out of `root`/access.json: 200; 404 when there is none; 409 when it is the last admin
	// token (nobody could manage the server then); 500 with `error` when the file cannot be read or written.
	int32 RevokeConfigToken(const FS::Path& root, Core::StringView name, Core::String& error);
	// A token's name: 1 to 64 letters, digits, '.', '_' and '-'.
	bool IsConfigTokenName(Core::StringView name);
	// The token an Authorization header carries ("Bearer <token>", the scheme in any case); empty when none.
	Core::StringView BearerToken(Core::StringView header);
	// The SHA-256 of `token`'s text, hex.
	Core::String TokenDigest(Core::StringView token);

	const char* c_AccessFile = "access.json";
}
