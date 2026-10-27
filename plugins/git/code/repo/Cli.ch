// Running the git CLI (docs/10 "Git", docs/99 #14): every call gets `-c core.quotepath=off -c color.ui=never`, never
// prompts on a terminal (GIT_TERMINAL_PROMPT=0), takes no optional locks (a status never rewrites the index), keeps
// git's own messages in English (LC_ALL=C) and has `:` for an editor. The server lock must not be held while git runs.

include "bedrock.ch"

namespace Git
{
	const int32 c_GitReadTimeoutMs = 30000;
	const int32 c_GitWriteTimeoutMs = 120000;

	struct GitRun
	{
		public int64 m_ExitCode;
		public Core::String m_Output;
		public Core::String m_Errors;

		public bool Ok() const { return m_ExitCode == 0; }
		// What went wrong in git's words (its error output, or why it could not run).
		public Core::String Problem() const;
	}

	class GitCli
	{
		public Core::String m_Program = Core::String("git");   // the git.path setting, or git on PATH

		// Runs git with `arguments` in `folder`.
		public GitRun Run(const FS::Path& folder, const Core::Array<Core::String>& arguments, int32 timeoutMs) const;
		// What starts a long one (fetch, pull, push) whose output is read as it comes; stderr joins stdout.
		public Process::StartInfo StartInfoFor(const FS::Path& folder, const Core::Array<Core::String>& arguments) const;
	}

	// The arguments as one list: GitArgs("status", "--porcelain=v2").
	Core::Array<Core::String> GitArgs(const char* a);
	Core::Array<Core::String> GitArgs(const char* a, const char* b);
	Core::Array<Core::String> GitArgs(const char* a, const char* b, const char* c);
	Core::Array<Core::String> GitArgs(const char* a, const char* b, const char* c, const char* d);
	// "git a b c" as a person would type it, for the Git output.
	Core::String GitCommandLine(const Core::Array<Core::String>& arguments);
}
